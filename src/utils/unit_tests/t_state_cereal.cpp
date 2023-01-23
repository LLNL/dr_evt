/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#if defined(DR_EVT_HAS_CEREAL)
#include <iostream>
#include <sstream>
#include <random>
#include "utils/seed.hpp"
#include "utils/state_io_cereal.hpp"
ENABLE_CUSTOM_CEREAL (std::minstd_rand);

#if defined(DR_EVT_HAS_CATCH2)
#include <cstddef>
#include "catch2/catch.hpp"
#endif // defined(DR_EVT_HAS_CATCH2)

using namespace dr_evt;

struct MyClass2
{
    int a;
    MyClass2() : a(0) {}

    template<class Archive>
    void serialize(Archive & archive)
    {
        archive(a);
    }
};

struct MyClass
{
    using generator_t = std::minstd_rand;
    using char_t = char;

    uint64_t x;
    double y;
    char z;
    MyClass2 m;
    generator_t gen;

    MyClass() : x(0ull), y(0.0), z('\0') {}

    MyClass(uint64_t v1, double v2, char v3)
    : x(v1), y(v3), z(v3) {}

    MyClass(const MyClass& rhs)
    : x(rhs.x), y(rhs.y), z(rhs.z), m(rhs.m), gen(rhs.gen) {}

    MyClass& operator=(const MyClass& rhs)
    {
        if (this == &rhs) {
            return (*this);
        }
        x = rhs.x;
        y = rhs.y;
        z = rhs.z;
        m = rhs.m;
        gen = rhs.gen;
        return (*this);
    }

    void init()
    {
        const auto sseqi = make_seed_seq_input(x, y, z);
        std::seed_seq sseq(sseqi.begin(), sseqi.end());
        gen.seed(sseq);
    }

    // This method lets cereal know which data members to serialize
    template<class Archive>
    void serialize(Archive & archive)
    {
        archive( x, y, z, m, gen ); // serialize things by passing them to the archive
    }

    static void print_size()
    {
        std::cout << "sizeof(MyClass::x) + sizeof(MyClass::y) + sizeof(MyClass::z)"
                  << "+ sizeof(MyClass::m::a) + sizeof(MyClass::gen) = "
                  << sizeof(x) + sizeof(y) + sizeof(z) + sizeof (MyClass2::a) + sizeof(gen)
                  << std::endl;
        std::cout << "sizeof(MyClass) " << sizeof(MyClass) << std::endl;
    }

    void print() const
    {
        std::cout << "MyClass.x = " << x << std::endl;
        std::cout << "MyClass.y = " << y << std::endl;
        std::cout << "MyClass.z = " << z << std::endl;
        std::cout << "MyClass.a = " << m.a << std::endl;
    }

    void show_4rns()
    {
        std::cout << "MyClass.gen 4rns: "
                  << gen() << ' ' << gen() << ' ' << gen() << ' ' << std::endl;
    }
};



void use_stringstream(int n = 1)
{
    uint64_t v = 1ull;
    for (int i = 0; i < n ; i++)
    {
        v *= 10;
        std::stringstream ss;
        {
            std::cout << i << " save ==================" << std::endl;
            MyClass m {v, 3.14, static_cast<char>('a'+ (char)i)};
            m.m.a = 4;
            m.init();
            m.print();
            save_state(m, ss);
            m.show_4rns();
        }
        std::cout << "ss.str().size(): " << ss.str().size() << std::endl;
        {
            std::cout << i << " load ------------------" << std::endl;
            MyClass mm;
            load_state(mm, ss);
            mm.print();
            mm.show_4rns();
        }
    }
}

void use_streamvec(int n = 1)
{
    uint64_t v = 1ull;
    for (int i = 0; i < n ; i++)
    {
        v *= 10;
        // Pre-allocate space. Size the vector such that it can accomodate the whole
        // state without reallocation.
        constexpr size_t state_size = sizeof(MyClass::x) + sizeof(MyClass::y)
                                    + sizeof(MyClass::z) + sizeof(MyClass2::a)
                                    + sizeof(MyClass::gen);
    #if 0
        std::vector<char> buf(state_size);
    #else
        std::vector<char> buf;
        buf.reserve(state_size);
    #endif
        {
            dr_evt::ostreamvec<char> ostrmbuf(buf);
            std::ostream os(&ostrmbuf);

            std::cout << i << " save ==================" << std::endl;
            MyClass m {v, 3.14, static_cast<char>('a'+ (char)i)};
            m.m.a = 4;
            m.init();
            m.print();
            save_state(m, os);
            m.show_4rns();
        }
        std::cout << "buf.size(): " << buf.size() << std::endl;
        {
            dr_evt::istreamvec<char> istrmbuf(buf);
            std::istream is(&istrmbuf);

            std::cout << i << " load ------------------" << std::endl;
            MyClass mm;
            load_state(mm, is);
            mm.print();
            mm.show_4rns();
        }
    }
}

void use_streambuff(int n = 1)
{
    // Pre-allocate the buffer such that it can accomodate the whole state
    constexpr size_t state_size = sizeof(MyClass::x) + sizeof(MyClass::y)
                                + sizeof(MyClass::z) + sizeof(MyClass2::a)
                                + sizeof(MyClass::gen);
    uint64_t v = 1ull;
    char* buf = new char [state_size];

    for (int i = 0; i < n ; i++)
    {
        v *= 10;
        {
            dr_evt::ostreambuff<char> ostrmbuf(buf, state_size);
            std::ostream os(&ostrmbuf);

            std::cout << i << " save ==================" << std::endl;
            MyClass m {v, 3.14, static_cast<char>('a'+ (char)i)};
            m.m.a = 4;
            m.init();
            m.print();
            save_state(m, os);
            m.show_4rns();
        }
        std::cout << "buf size: " << state_size << std::endl;
        {
            dr_evt::istreambuff<char> istrmbuf(buf, state_size);
            std::istream is(&istrmbuf);

            std::cout << i << " load ------------------" << std::endl;
            MyClass mm;
            load_state(mm, is);
            mm.print();
            mm.show_4rns();
        }
    }
    delete [] buf;
}

int main()
{

    MyClass::print_size();

    use_stringstream();
    use_streamvec();
    use_streambuff();

    is_custom_bin_cerealizable<MyClass2>();
    is_custom_bin_cerealizable<MyClass::generator_t>();
    is_custom_bin_cerealizable<double>();
    return 0;
}
#endif // !defined(DR_EVT_HAS_CEREAL)
