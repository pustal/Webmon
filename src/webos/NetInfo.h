/**
 * @file NetInfo.h
 * @brief Small network helpers: the TV's LAN address, and whether a client
 *        address belongs to a private network.
 */
#ifndef WEBMON_NET_INFO_H
#define WEBMON_NET_INFO_H

#include <string>

namespace webmon
{

/**
 * Best guess at the address other devices on the LAN can reach the TV on:
 * the first IPv4 address of an interface that is up and not loopback,
 * preferring wired and Wi-Fi interfaces.
 *
 * @return Dotted-quad address, or an empty string when offline.
 */
std::string FindLanAddress();

/**
 * Whether @p address is loopback, link-local or in a private (RFC 1918 /
 * unique-local) range. The upload page only answers such clients.
 */
bool IsPrivateNetworkAddress(const std::string &address);

} // namespace webmon

#endif // WEBMON_NET_INFO_H
