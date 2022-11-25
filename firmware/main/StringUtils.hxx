/** \copyright
 * Copyright (c) 2021, Mike Dunston
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are  permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file StringUtils.hxx
 *
 * String manipulation utility methods.
 *
 * @author Mike Dunston
 * @date 23 July 2021
 */

#ifndef STRINGUTILS_HXX_
#define STRINGUTILS_HXX_

#include <algorithm>
#include <string>
#include <utils/format_utils.hxx>

/// Utility function to inject a separator into a string at a specified
/// interval.
///
/// @param input is the string to be manipulated.
/// @param num is the interval at which to insert the separator.
/// @param separator is the character to insert.
template <const unsigned num, const char separator>
static inline void inject_seperator(std::string &input)
{
    for (auto it = input.begin(); (num + 1) <= std::distance(it, input.end()); ++it)
    {
        std::advance(it, num);
        it = input.insert(it, separator);
    }
}

/// Converts a string to an unsigned 64bit integer removing "." characters.
///
/// @param value String to convert.
/// @return uint64_t version of @param value.
static inline uint64_t string_to_uint64(std::string value)
{
    // remove period characters if present
    value.erase(std::remove(value.begin(), value.end(), '.'), value.end());
    // convert the string to a uint64_t value
    return std::stoull(value, nullptr, 16);
}

/// Converts an OpenLCB Node ID to a string format injecting a "." every two
/// characters.
///
/// @param id Node ID to convert.
/// @return String representation of the Node ID.
static inline std::string node_id_to_string(uint64_t id)
{
    std::string result = uint64_to_string_hex(id, 12);
    std::replace(result.begin(), result.end(), ' ', '0');
    inject_seperator<2, '.'>(result);
    return result;
}

/// Converts an OpenLCB Event ID to a string format injecting a "." every two
/// characters.
///
/// @param id Event ID to convert.
/// @return String representation of the Event ID.
static inline std::string event_id_to_string(uint64_t id)
{
    std::string result = uint64_to_string_hex(id, 16);
    std::replace(result.begin(), result.end(), ' ', '0');
    inject_seperator<2, '.'>(result);
    return result;
}

/// Modifies (in place) a string to remove null (\0), 0xFF and trailing spaces.
///
/// @param value String to be modified.
/// @param drop_eol When enabled `\r` and `\n` will be removed.
static inline void remove_nulls_and_FF(std::string &value, bool drop_eol = false)
{
    // replace null characters with spaces so the browser can parse the
    // XML successfully.
    std::replace(value.begin(), value.end(), '\0', ' ');

    // remove 0xff which is a default value for EEPROM data.
    std::replace(value.begin(), value.end(), (char)0xFF, ' ');

    if (drop_eol)
    {
        // replace newline characters with spaces.
        std::replace(value.begin(), value.end(), '\n', ' ');

        // replace carriage return characters with spaces.
        std::replace(value.begin(), value.end(), '\r', ' ');
    }
}

#endif // STRINGUTILS_HXX_