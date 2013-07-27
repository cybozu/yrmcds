// Unit test class.
// (C) 2008 Cybozu Labs, Inc., all rights reserved.
// (C) 2013 Cybozu.  All rights reserved.

#ifndef CYBOZU_TEST_HPP
#define CYBOZU_TEST_HPP

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <list>
#include <stdio.h>
#include <string.h>
#include <string>
#include <tuple>
#include <utility>

namespace cybozu { namespace test {

class AutoRun {
    typedef void (*Func)();
    typedef std::list<std::pair<const char*, Func> > UnitTestList;
public:
    AutoRun()
        : init_(0)
        , term_(0)
        , okCount_(0)
        , ngCount_(0)
        , exceptionCount_(0)
    {
    }
    void setup(Func init, Func term)
    {
        init_ = init;
        term_ = term;
    }
    void append(const char *name, Func func)
    {
        list_.push_back(std::make_pair(name, func));
    }
    void set(bool isOK)
    {
        if (isOK) {
            okCount_++;
        } else {
            ngCount_++;
        }
    }
    std::string getBaseName(const std::string& name) const
    {
#ifdef _WIN32
        const char sep = '\\';
#else
        const char sep = '/';
#endif
        size_t pos = name.find_last_of(sep);
        std::string ret = name.substr(pos + 1);
        pos = ret.find('.');
        return ret.substr(0, pos);
    }
    using ret_t = std::tuple<int, std::uint64_t>;
    ret_t run(const char* arg0)
    {
        using namespace std::chrono;
        std::string msg;
        steady_clock::duration elapsed;
        try {
            if (init_) init_();
            for (UnitTestList::const_iterator i = list_.begin(), ie = list_.end(); i != ie; ++i) {
                std::cout << "ctest:module=" << i->first << std::endl;
                try {
                    auto t1 = steady_clock::now();
                    (i->second)();
                    auto t2 = steady_clock::now();
                    elapsed += t2 - t1;
                } catch (std::exception& e) {
                    exceptionCount_++;
                    std::cout << "ctest:  " << i->first << " is stopped by std::exception " << e.what() << std::endl;
                } catch (...) {
                    exceptionCount_++;
                    std::cout << "ctest:  " << i->first << " is stopped by an exception" << std::endl;
                }
            }
            if (term_) term_();
        } catch (...) {
            msg = "ctest:err: catch unexpected exception";
        }
        fflush(stdout);
        std::uint64_t elapsed_us = duration_cast<microseconds>(elapsed).count();
        if (msg.empty()) {
            std::cout << "ctest:name=" << getBaseName(arg0)
                      << ", module=" << list_.size()
                      << ", total=" << (okCount_ + ngCount_ + exceptionCount_)
                      << ", ok=" << okCount_
                      << ", ng=" << ngCount_
                      << ", exception=" << exceptionCount_ << std::endl;
            return (ngCount_>0) ? ret_t(1, elapsed_us) : ret_t(0, elapsed_us);
        } else {
            std::cout << msg << std::endl;
            return ret_t(1, elapsed_us);
        }
    }
    static inline AutoRun& getInstance()
    {
        static AutoRun instance;
        return instance;
    }
private:
    Func init_;
    Func term_;
    int okCount_;
    int ngCount_;
    int exceptionCount_;
    std::uint64_t elapsed_;
    UnitTestList list_;
};

static AutoRun& autoRun = AutoRun::getInstance();

inline void test(bool ret, const std::string& msg, const std::string& param, const char *file, int line)
{
    autoRun.set(ret);
    if (!ret) {
        std::cout << "ctest:" << file << ":" << line << " "
                  << msg << param << ";" << std::endl;
    }
}

template<typename T, typename U>
bool isEqual(const T& lhs, const U& rhs)
{
    return lhs == rhs;
}

inline bool isEqual(const char *lhs, const char *rhs)
{
    return strcmp(lhs, rhs) == 0;
}
inline bool isEqual(char *lhs, const char *rhs)
{
    return strcmp(lhs, rhs) == 0;
}
inline bool isEqual(const char *lhs, char *rhs)
{
    return strcmp(lhs, rhs) == 0;
}
inline bool isEqual(char *lhs, char *rhs)
{
    return strcmp(lhs, rhs) == 0;
}
// avoid to compare float directly
inline bool isEqual(float lhs, float rhs)
{
    union fi {
        float f;
        uint32_t i;
    } lfi, rfi;
    lfi.f = lhs;
    rfi.f = rhs;
    return lfi.i == rfi.i;
}
// avoid to compare double directly
inline bool isEqual(double lhs, double rhs)
{
    union di {
        double d;
        uint64_t i;
    } ldi, rdi;
    ldi.d = lhs;
    rdi.d = rhs;
    return ldi.i == rdi.i;
}

} } // cybozu::test

#define TEST_MAIN(arg_parser)                                          \
    int main(int argc, char** argv) {                                  \
        if( ! arg_parser(argc, argv) )                                 \
            return 0;                                                  \
        int r;                                                         \
        std::uint64_t elapsed;                                         \
        std::tie(r, elapsed) = cybozu::test::autoRun.run(argv[0]);     \
        std::cerr << "ctest:elapsed=" << elapsed << "us" << std::endl; \
        return r;                                                      \
    }

#ifndef TEST_DISABLE_AUTO_RUN
bool null_parser(int argc, char** argv) { return true; }
TEST_MAIN(null_parser);
#endif

/**
    alert if !x
    @param x [in]
*/
#define cybozu_assert(...) cybozu_assert_((__VA_ARGS__))
#define cybozu_assert_(x) cybozu::test::test(!!(x), "cybozu_assert", #x, __FILE__, __LINE__)

/**
    alert if x != y
    @param x [in]
    @param y [in]
*/
#define cybozu_equal(x, y) { \
    bool eq = cybozu::test::isEqual(x, y); \
    cybozu::test::test(eq, "cybozu_equal", "(" #x ", " #y ")", __FILE__, __LINE__); \
    if (!eq) { \
        std::cout << "ctest:  lhs=" << (x) << std::endl; \
        std::cout << "ctest:  rhs=" << (y) << std::endl; \
    } \
}
/**
    alert if fabs(x, y) >= eps
    @param x [in]
    @param y [in]
*/
#define cybozu_near(x, y, eps) { \
    bool isNear = fabs((x) - (y)) < eps; \
    cybozu::test::test(isNear, "cybozu_near", "(" #x ", " #y ")", __FILE__, __LINE__); \
    if (!isNear) { \
        std::cout << "ctest:  lhs=" << (x) << std::endl; \
        std::cout << "ctest:  rhs=" << (y) << std::endl; \
    } \
}

#define cybozu_equal_pointer(x, y) { \
    bool eq = x == y; \
    cybozu::test::test(eq, "cybozu_equal_pointer", "(" #x ", " #y ")", __FILE__, __LINE__); \
    if (!eq) { \
        std::cout << "ctest:  lhs=" << (const void*)(x) << std::endl; \
        std::cout << "ctest:  rhs=" << (const void*)(y) << std::endl; \
    } \
}

/**
    always alert
    @param msg [in]
*/
#define cybozu_fail(msg) cybozu::test::test(false, "cybozu_fail", "(" msg ")", __FILE__, __LINE__)

/**
    verify message in exception
*/
#define cybozu_test_exception_message(statement, Exception, msg) \
{ \
    int ret = 0; \
    std::string errMsg; \
    try { \
        statement; \
        ret = 1; \
    } catch (const Exception& e) { \
        errMsg = e.what(); \
        if (errMsg.find(msg) == std::string::npos) { \
            ret = 2; \
        } \
    } catch (...) { \
        ret = 3; \
    } \
    if (ret) { \
        cybozu::test::test(false, "cybozu_test_exception_message", "(" #statement ", " #Exception ", " #msg ")", __FILE__, __LINE__); \
        if (ret == 1) { \
            std::cout << "ctest:  no exception" << std::endl; \
        } else if (ret == 2) { \
            std::cout << "ctest:  bad exception msg:" << errMsg << std::endl; \
        } else { \
            std::cout << "ctest:  unexpected exception" << std::endl; \
        } \
    } else { \
        cybozu::test::autoRun.set(true); \
    } \
}

#define cybozu_test_exception(statement, Exception) \
{ \
    int ret = 0; \
    try { \
        statement; \
        ret = 1; \
    } catch (const Exception&) { \
    } catch (...) { \
        ret = 2; \
    } \
    if (ret) { \
        cybozu::test::test(false, "cybozu_test_exception", "(" #statement ", " #Exception ")", __FILE__, __LINE__); \
        if (ret == 1) { \
            std::cout << "ctest:  no exception" << std::endl; \
        } else { \
            std::cout << "ctest:  unexpected exception" << std::endl; \
        } \
    } else { \
        cybozu::test::autoRun.set(true); \
    } \
}

/**
    verify statement does not throw
*/
#define cybozu_test_no_exception(statement) \
try { \
    statement; \
    cybozu::test::autoRun.set(true); \
} catch (...) { \
    cybozu::test::test(false, "cybozu_test_no_exception", "(" #statement ")", __FILE__, __LINE__); \
}

/**
    append auto unit test
    @param name [in] module name
*/
#define AUTOTEST(name) \
void cybozu_test_ ## name(); \
struct cybozu_test_local_ ## name { \
    cybozu_test_local_ ## name() \
    { \
        cybozu::test::autoRun.append(#name, cybozu_test_ ## name); \
    } \
} cybozu_test_local_instance_ ## name; \
void cybozu_test_ ## name()

/**
    append auto unit test with fixture
    @param name [in] module name
*/
#define AUTOTEST_WITH_FIXTURE(name, Fixture) \
void cybozu_test_ ## name(); \
void cybozu_test_real_ ## name() \
{ \
    Fixture f; \
    cybozu_test_ ## name(); \
} \
struct cybozu_test_local_ ## name { \
    cybozu_test_local_ ## name() \
    { \
        cybozu::test::autoRun.append(#name, cybozu_test_real_ ## name); \
    } \
} cybozu_test_local_instance_ ## name; \
void cybozu_test_ ## name()

/**
    setup fixture
    @param Fixture [in] class name of fixture
    @note cstr of Fixture is called before test and dstr of Fixture is called after test
*/
#define SETUP_FIXTURE(Fixture) \
Fixture *cybozu_test_local_fixture; \
void cybozu_test_local_init() \
{ \
    cybozu_test_local_fixture = new Fixture(); \
} \
void cybozu_test_local_term() \
{ \
    delete cybozu_test_local_fixture; \
} \
struct cybozu_test_local_fixture_setup_ { \
    cybozu_test_local_fixture_setup_() \
    { \
        cybozu::test::autoRun.setup(cybozu_test_local_init, cybozu_test_local_term); \
    } \
} cybozu_test_local_fixture_setup_instance_;

#endif // CYBOZU_TEST_HPP
