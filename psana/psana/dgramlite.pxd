cdef struct Xtc:
    int junks[2]
    unsigned extent

cdef struct Sequence:
    unsigned low
    unsigned high

cdef struct Dgram:
    Sequence seq
    unsigned env
    Xtc xtc

