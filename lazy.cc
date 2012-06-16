#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/signals2.hpp>
#include <cassert>

#ifndef _XOPEN_SOURCE
# define _XOPEN_SOURCE 1
#endif
#include <iostream>
#include <vector>
#include <string>
#include <ucontext.h>

template<class T>
class Lazy
{
    enum State
    {
        LAZY_NOT_READY,
        LAZY_READY,
        LAZY_FAIL
    };

    struct Impl
        : public boost::enable_shared_from_this<Impl>
    {
        State state_;
        T value_;

        boost::signals2::signal<void(T)> ready_callbacks_;
        boost::signals2::signal<void()> fail_callbacks_;

        Impl()
            : state_(LAZY_NOT_READY)
            , value_(T())
        {
        }

        void on_ready(boost::function<void(T)> ready_callback)
        {
            ready_callbacks_.connect(ready_callback);
            if (state_ == LAZY_READY)
            {
                ready_callback(value_);
            }
        }

        void on_fail(boost::function<void()> fail_callback)
        {
            fail_callbacks_.connect(fail_callback);
            if (state_ == LAZY_FAIL)
            {
                fail_callback();
            }
        }

        void emit_ready()
        {
            state_ = LAZY_READY;
            ready_callbacks_(value_);
        }

        void emit_fail()
        {
            p_impl_->state_ = LAZY_FAIL;
            fail_callbacks_();
        }

        void operator_plus_step2(int b)
        {
            value_ += b;
            emit_ready();
        }

        void operator_plus_step1(int a, Lazy<T>& b)
        {
            value_ = a;
            b.on_ready(boost::bind(&Impl::operator_plus_step2, this->shared_from_this(), _1));
        }
    };

public:
    explicit Lazy()
        : p_impl_(new Impl())
    {
    }

    Lazy(const T& value)
        : p_impl_(new Impl())
    {
        p_impl_->value_ = value;
        p_impl_->emit_ready();
    }

    T get() const
    {
        assert(p_impl_->state_ == LAZY_READY);
        return p_impl_->value_;
    }

    void on_ready(boost::function<void(T)> ready_callback)
    {
        p_impl_->on_ready(ready_callback);
    }

    void on_fail(boost::function<void()> fail_callback)
    {
        p_impl_->on_fail(fail_callback);
    }

    void emit_ready()
    {
        p_impl_->emit_ready();
    }

    void emit_fail()
    {
        p_impl_->emit_fail();
    }

    Lazy<T>& operator=(const T& value)
    {
        assert(p_impl_->state_ == LAZY_NOT_READY);

        p_impl_->value_ = value;
        p_impl_->emit_ready();
        return *this;
    }

    Lazy<T> operator+(const Lazy<T>& b)
    {
        Lazy<T> result;
        on_ready(boost::bind(&Impl::operator_plus_step1, result.p_impl_, _1, b));
        return result;
    }

private:
    boost::shared_ptr<Impl> p_impl_;
};

class Wait
    : public boost::enable_shared_from_this<Wait>
{
public:
    Wait()
        : counter_(0)
    {
    }

    template<class T>
    Wait& operator()(Lazy<T>& lazy_value)
    {
        ++counter_;
        lazy_value.on_ready(boost::bind(&Wait::one_ready, shared_from_this()));
        return *this;
    }

    Wait& run(boost::function<void()> job)
    {
        // Lock

        jobs_.push_back(job);
        if (counter_ == 0)
        {
            job();
        }
    }

private:
    int counter_;
    std::vector<boost::function<void()> > jobs_;

    void one_ready()
    {
        // Lock

        --counter_;
        if (counter_ == 0)
        {
            BOOST_FOREACH(boost::function<void()> job, jobs_)
            {
                job();
            }
        }
    }
};

ucontext_t return_context, main_context, coroutine_context;
char iterator_stack[SIGSTKSZ];

void __async_continue()
{
#ifdef DEBUG
    std::cout << "[async continue]" << std::endl;
#endif
    swapcontext(&main_context, &coroutine_context);
}

template<class T>
T __do_async_wait(const char* name, Lazy<T> var)
{
#ifdef DEBUG
    std::cout << "[async wait " << name << "]" << std::endl;
#endif
    var.on_ready(boost::bind(&__async_continue));
    swapcontext(&coroutine_context, &main_context);
#ifdef DEBUG
    std::cout << "[async ready " << name << "=" << var.get() << "]" << std::endl;
#endif
    return var.get();
}

#define __async_wait(var) __do_async_wait(#var, var)

#define START_COROUTINE_AND_RUN_ONCE(coroutine_function) \
    volatile int coroutine_finished = 0; \
    {\
        int res; \
        res = getcontext(&coroutine_context); \
        assert(res != -1); \
        coroutine_context.uc_link = &return_context; \
        coroutine_context.uc_stack.ss_sp = iterator_stack; \
        coroutine_context.uc_stack.ss_size = sizeof(iterator_stack); \
        makecontext(&coroutine_context, coroutine_function, 0); \
        \
        res = getcontext(&return_context); \
        assert(res != -1); \
    } \
    if (coroutine_finished || ((coroutine_finished = 1), swapcontext(&main_context, &coroutine_context))) {} \
    else

void print(std::string name, int a)
{
    std::cout << name << ": " << a << std::endl;
}

Lazy<int> a, b, c, d;

void test_coroutine()
{
    std::cout << "inside coroutine" << std::endl;
    std::cout << __async_wait(a) << "," << __async_wait(b) << std::endl;
    std::cout << __async_wait(c) << std::endl;
}

int main()
{
    c = a + 1;
    d = a + b + c + c;

    START_COROUTINE_AND_RUN_ONCE(test_coroutine)
    {
        a = 10;
        b = 5;
    }

    return 0;
}
