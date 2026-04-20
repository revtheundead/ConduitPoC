#ifndef BGEN11_ENDIAN_HPP
#define BGEN11_ENDIAN_HPP

namespace bgen11 {
namespace io {

enum Endian {
    Endian_Big = 0,
    Endian_Little = 1
};

struct EndianTag {
    static const int Big = Endian_Big;
    static const int Little = Endian_Little;
};

}
}

#define BGEN11_ENDIAN_BIG    ::bgen11::io::Endian_Big
#define BGEN11_ENDIAN_LITTLE ::bgen11::io::Endian_Little

#endif
