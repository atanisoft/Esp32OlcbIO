/** \copyright
 * Copyright (c) 2020, Mike Dunston
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
 * \file FactoryResetHelper.hxx
 *
 * Resets the node name and description based on the configured Node ID.
 *
 * @author Mike Dunston
 * @date 10 November 2020
 */

#ifndef FACTORY_RESET_HELPER_HXX_
#define FACTORY_RESET_HELPER_HXX_

#include "cdi.hxx"

#include <algorithm>
#include <stdint.h>
#include <executor/Notifiable.hxx>
#include <utils/ConfigUpdateListener.hxx>
#include <utils/format_utils.hxx>

namespace esp32io
{

// when the io board starts up the first time the config is blank and needs to
// be reset to factory settings.
class FactoryResetHelper : public DefaultConfigUpdateListener
{
public:
    FactoryResetHelper(const esp32io::ConfigDef &cfg, uint64_t node_id)
        : cfg_(cfg), nodeID_(node_id)
    {

    }

    UpdateAction apply_configuration(int fd, bool initial_load,
                                     BarrierNotifiable *done) override
    {
        // nothing to do here as we do not load config
        AutoNotify n(done);
        LOG(VERBOSE, "[CFG] apply_configuration(%d, %d)", fd, initial_load);
        return UPDATED;
    }

    void factory_reset(int fd) override
    {
        LOG(VERBOSE, "[CFG] factory_reset(%d)", fd);
        cfg_.userinfo().name().write(fd, SNIP_PROJECT_NAME);
        string node_id = uint64_to_string_hex(nodeID_, 12);
        std::replace(node_id.begin(), node_id.end(), ' ', '0');
        inject_seperator<2, '.'>(node_id);
        cfg_.userinfo().description().write(fd, node_id.c_str());
    }
private:
    const esp32io::ConfigDef &cfg_;
    const uint64_t nodeID_;

    template<const unsigned num, const char separator>
    void inject_seperator(std::string & input)
    {
        for (auto it = input.begin(); (num + 1) <= std::distance(it, input.end());
            ++it)
        {
            std::advance(it, num);
            it = input.insert(it, separator);
        }
    }
};

} // namespace esp32io

#endif // FACTORY_RESET_HELPER_HXX_