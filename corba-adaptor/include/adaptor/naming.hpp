// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Naming Service Helper (C++11)

#ifndef ADAPTOR_NAMING_HPP
#define ADAPTOR_NAMING_HPP

#include <string>

#include <orbsvcs/CosNamingC.h>
#include <tao/ORB.h>

namespace adaptor {

class NamingHelper {
public:
    explicit NamingHelper(CORBA::ORB_ptr orb);

    NamingHelper(const NamingHelper&);
    NamingHelper& operator=(const NamingHelper&);

    void bind(const std::string& path, CORBA::Object_ptr obj);
    void unbind(const std::string& path);
    bool is_valid() const;

private:
    CosNaming::NamingContextExt_var root_;
};

} // namespace adaptor

#endif
