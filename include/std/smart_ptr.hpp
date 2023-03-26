#ifndef __FENNIX_KERNEL_STD_SMART_POINTER_H__
#define __FENNIX_KERNEL_STD_SMART_POINTER_H__

#include <types.h>

#include <debug.h>

// show debug messages
// #define DEBUG_SMARTPOINTERS 1

#ifdef DEBUG_SMARTPOINTERS
#define spdbg(m, ...) debug(m, ##__VA_ARGS__)
#else
#define spdbg(m, ...)
#endif

namespace std
{
    /**
     * @brief A smart pointer class
     *
     * This class is a smart pointer class. It is used to manage the lifetime of
     * objects. It is a reference counted pointer, so when the last reference to
     * the object is removed, the object is deleted.
     *
     * Basic Usage:
     * smart_ptr<char> pointer(new char());
     * *pointer = 'a';
     * printf("%c", *pointer); // Prints "a"
     */
    template <class T>
    class smart_ptr
    {
        T *m_RealPointer;

    public:
        explicit smart_ptr(T *Pointer = nullptr)
        {
            spdbg("Smart pointer created (%#lx)", m_RealPointer);
            m_RealPointer = Pointer;
        }

        ~smart_ptr()
        {
            spdbg("Smart pointer deleted (%#lx)", m_RealPointer);
            delete m_RealPointer, m_RealPointer = nullptr;
        }

        T &operator*()
        {
            spdbg("Smart pointer dereferenced (%#lx)", m_RealPointer);
            return *m_RealPointer;
        }

        T *operator->()
        {
            spdbg("Smart pointer dereferenced (%#lx)", m_RealPointer);
            return m_RealPointer;
        }

        T *get()
        {
            spdbg("Smart pointer returned (%#lx)", m_RealPointer);
            return m_RealPointer;
        }
    };

    template <class T>
    class auto_ptr
    {
    };

    template <class T>
    class unique_ptr
    {
    };

    template <class T>
    class weak_ptr
    {
    };

    template <typename T>
    class shared_ptr
    {
    private:
        class counter
        {
        private:
            unsigned int m_RefCount{};

        public:
            counter() : m_RefCount(0) { spdbg("Counter %#lx created", this); };
            counter(const counter &) = delete;
            counter &operator=(const counter &) = delete;
            ~counter() { spdbg("Counter %#lx deleted", this); }
            void reset()
            {
                m_RefCount = 0;
                spdbg("reset");
            }

            unsigned int get()
            {
                return m_RefCount;
                spdbg("return");
            }

            void operator++()
            {
                m_RefCount++;
                spdbg("increment");
            }

            void operator++(int)
            {
                m_RefCount++;
                spdbg("increment");
            }

            void operator--()
            {
                m_RefCount--;
                spdbg("decrement");
            }

            void operator--(int)
            {
                m_RefCount--;
                spdbg("decrement");
            }
        };

        counter *m_ReferenceCounter;
        T *m_RealPointer;

    public:
        explicit shared_ptr(T *Pointer = nullptr)
        {
            m_RealPointer = Pointer;
            m_ReferenceCounter = new counter();
            spdbg("[%#lx] Shared pointer created (ptr=%#lx, ref=%#lx)", this, Pointer, m_ReferenceCounter);
            if (Pointer)
                (*m_ReferenceCounter)++;
        }

        shared_ptr(shared_ptr<T> &SPtr)
        {
            spdbg("[%#lx] Shared pointer copied (ptr=%#lx, ref=%#lx)", this, SPtr.m_RealPointer, SPtr.m_ReferenceCounter);
            m_RealPointer = SPtr.m_RealPointer;
            m_ReferenceCounter = SPtr.m_ReferenceCounter;
            (*m_ReferenceCounter)++;
        }

        ~shared_ptr()
        {
            spdbg("[%#lx] Shared pointer destructor called", this);
            (*m_ReferenceCounter)--;
            if (m_ReferenceCounter->get() == 0)
            {
                spdbg("[%#lx] Shared pointer deleted (ptr=%#lx, ref=%#lx)", this, m_RealPointer, m_ReferenceCounter);
                delete m_ReferenceCounter, m_ReferenceCounter = nullptr;
                delete m_RealPointer, m_RealPointer = nullptr;
            }
        }

        unsigned int get_count()
        {
            spdbg("[%#lx] Shared pointer count (%d)", this, m_ReferenceCounter->get());
            return m_ReferenceCounter->get();
        }

        T *get()
        {
            spdbg("[%#lx] Shared pointer get (%#lx)", this, m_RealPointer);
            return m_RealPointer;
        }

        T &operator*()
        {
            spdbg("[%#lx] Shared pointer dereference (ptr*=%#lx)", this, *m_RealPointer);
            return *m_RealPointer;
        }

        T *operator->()
        {
            spdbg("[%#lx] Shared pointer dereference (ptr->%#lx)", this, m_RealPointer);
            return m_RealPointer;
        }

        void reset(T *Pointer = nullptr)
        {
            if (m_RealPointer == Pointer)
                return;
            spdbg("[%#lx] Shared pointer reset (ptr=%#lx, ref=%#lx)", this, Pointer, m_ReferenceCounter);
            (*m_ReferenceCounter)--;
            if (m_ReferenceCounter->get() == 0)
            {
                delete m_RealPointer;
                delete m_ReferenceCounter;
            }
            m_RealPointer = Pointer;
            m_ReferenceCounter = new counter();
            if (Pointer)
                (*m_ReferenceCounter)++;
        }

        void reset()
        {
            spdbg("[%#lx] Shared pointer reset (ptr=%#lx, ref=%#lx)", this, m_RealPointer, m_ReferenceCounter);
            if (m_ReferenceCounter->get() == 1)
            {
                delete m_RealPointer, m_RealPointer = nullptr;
                delete m_ReferenceCounter, m_ReferenceCounter = nullptr;
            }
            else
            {
                (*m_ReferenceCounter)--;
            }
        }

        void swap(shared_ptr<T> &Other)
        {
            spdbg("[%#lx] Shared pointer swap (ptr=%#lx, ref=%#lx <=> ptr=%#lx, ref=%#lx)",
                  this, m_RealPointer, m_ReferenceCounter, Other.m_RealPointer, Other.m_ReferenceCounter);
            T *tempRealPointer = m_RealPointer;
            counter *tempReferenceCounter = m_ReferenceCounter;
            m_RealPointer = Other.m_RealPointer;
            m_ReferenceCounter = Other.m_ReferenceCounter;
            Other.m_RealPointer = tempRealPointer;
            Other.m_ReferenceCounter = tempReferenceCounter;
        }
    };

    template <typename T>
    struct remove_reference
    {
        typedef T type;
    };

    template <typename T>
    struct remove_reference<T &>
    {
        typedef T type;
    };

    template <typename T>
    struct remove_reference<T &&>
    {
        typedef T type;
    };

    template <typename T>
    using remove_reference_t = typename remove_reference<T>::type;

    template <typename T>
    T &&forward(remove_reference_t<T> &t) { return static_cast<T &&>(t); };

    template <typename T>
    T &&forward(remove_reference_t<T> &&t) { return static_cast<T &&>(t); };

    template <typename T, typename... Args>
    shared_ptr<T> make_shared(Args &&...args)
    {
        return shared_ptr<T>(new T(forward<Args>(args)...));
    };

    template <typename T, typename... Args>
    smart_ptr<T> make_smart(Args &&...args)
    {
        return smart_ptr<T>(new T(forward<Args>(args)...));
    };
}

#endif // !__FENNIX_KERNEL_STD_SMART_POINTER_H__