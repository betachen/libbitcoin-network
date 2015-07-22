/**
 * Copyright (c) 2011-2015 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * libbitcoin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License with
 * additional permissions to the one published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version. For more information see LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <bitcoin/bitcoin/config/authority.hpp>

#include <cstdint>
#include <string>
#include <sstream>
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/regex.hpp>
#include <bitcoin/bitcoin/formats/base16.hpp>
#include <bitcoin/bitcoin/primitives.hpp>
#include <bitcoin/bitcoin/utility/assert.hpp>
#include <bitcoin/bitcoin/utility/string.hpp>

namespace libbitcoin {
namespace config {

using namespace boost;
using namespace boost::asio;
using namespace boost::program_options;
using boost::format;

// hostname: [2001:db8::2] or 1.2.240.1
static std::string to_hostname(const std::string& host)
{
    if (host.find(":") == std::string::npos ||
        host.find("[") != std::string::npos)
        return host;

    const auto hostname = format("[%1%]") % host;
    return hostname.str();
}

// host: [2001:db8::2] or 2001:db8::2 or 1.2.240.1
static std::string to_authority(const std::string& host, uint16_t port)
{
    std::stringstream authority;
    authority << to_hostname(host);
    if (port > 0)
        authority << ":" << port;

    return authority.str();
}

static std::string to_ipv6(const std::string& ipv4_address)
{
    return std::string("::ffff:") + ipv4_address;
}

static ip::address_v6 to_ipv6(const ip::address_v4& ipv4_address)
{
    // Create an IPv6 mapped IPv4 address via serialization.
    const auto ipv6 = to_ipv6(ipv4_address.to_string());
    return ip::address_v6::from_string(ipv6);
}

static ip::address_v6 to_ipv6(const ip::address& ip_address)
{
    if (ip_address.is_v6())
        return ip_address.to_v6();

    BITCOIN_ASSERT_MSG(ip_address.is_v4(),
        "The address must be either IPv4 or IPv6.");

    return to_ipv6(ip_address.to_v4());
}

static std::string to_ipv4_hostname(const ip::address& ip_address)
{
    // std::regex requires gcc 4.9, so we are using boost::regex for now.
    static const regex regular("::ffff:([0-9\\.]+)");

    const auto address = ip_address.to_string();
    sregex_iterator it(address.begin(), address.end(), regular), end;
    if (it == end)
        return "";

    smatch match = *it;
    return match[1];
}

static std::string to_ipv6_hostname(const ip::address& ip_address)
{
    // IPv6 URLs use a bracketed IPv6 address, see rfc2732.
    const auto hostname = format("[%1%]") % to_ipv6(ip_address);
    return hostname.str();
}

authority::authority()
  : port_(0)
{
}

authority::authority(const authority& other)
  : authority(other.ip_, other.port_)
{
}

// authority: [2001:db8::2]:port or 1.2.240.1:port
authority::authority(const std::string& authority)
{
    std::stringstream(authority) >> *this;
}

// This is the format returned from peers on the bitcoin network.
authority::authority(const network_address_type& net)
  : authority(net.ip, net.port)
{
}

authority::authority(const ip_address_type& ip, uint16_t port)
  : ip_(ip::address_v6(ip)), port_(port)
{
}

// host: [2001:db8::2] or 2001:db8::2 or 1.2.240.1
authority::authority(const std::string& host, uint16_t port)
  : authority(to_authority(host, port))
{
}

authority::authority(const ip::address& ip, uint16_t port)
  : ip_(to_ipv6(ip)), port_(port)
{
}

authority::authority(const ip::tcp::endpoint& endpoint)
  : authority(endpoint.address(), endpoint.port())
{
}

ip_address_type authority::ip() const
{
    return ip_.to_bytes();
}

uint16_t authority::port() const
{
    return port_;
}

std::string authority::to_hostname() const
{
    auto ipv4_hostname = to_ipv4_hostname(ip_);
    return ipv4_hostname.empty() ? to_ipv6_hostname(ip_) : ipv4_hostname;
}

network_address_type authority::to_network_address() const
{
    static constexpr uint32_t services = 0;
    static constexpr uint64_t timestamp = 0;
    const network_address_type network_address
    {
        timestamp, services, ip(), port(),
    };

    return network_address;
}

std::string authority::to_string() const
{
    std::stringstream value;
    value << *this;
    return value.str();
}

bool authority::operator==(const authority& other) const
{
    return ip() == other.ip() && port() == other.port();
}

std::istream& operator>>(std::istream& input, authority& argument)
{
    std::string value;
    input >> value;

    // std::regex requires gcc 4.9, so we are using boost::regex for now.
    static const regex regular(
        "(([0-9\\.]+)|\\[([0-9a-f:\\.]+)])(:([0-9]{1,10}))?");

    sregex_iterator it(value.begin(), value.end(), regular), end;
    if (it == end)
    {
        BOOST_THROW_EXCEPTION(invalid_option_value(value));
    }

    boost::smatch match = *it;
    std::string ip_address(match[3]);
    if (ip_address.empty())
        ip_address = to_ipv6(match[2]);

    try
    {
        argument.ip_ = ip::address_v6::from_string(ip_address);
        argument.port_ = lexical_cast<uint16_t>(match[5]);
    }
    catch (const boost::exception&)
    {
        BOOST_THROW_EXCEPTION(invalid_option_value(value));
    }
    catch (const std::exception&)
    {
        BOOST_THROW_EXCEPTION(invalid_option_value(value));
    }

    return input;
}

std::ostream& operator<<(std::ostream& output, const authority& argument)
{
    output << to_authority(argument.to_hostname(), argument.port());
    return output;
}

} // namespace config
} // namespace libbitcoin
