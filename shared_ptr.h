#pragma once

#include <cstddef>
#include <type_traits>
#include <memory>

struct control_block {
    void release_ref() {
        ref_count--;
        if (ref_count == 0) {
            delete_object();
        }
    }
    
    void release_weak() {
        weak_count--;
    }

    void add_ref() {
        ref_count++;
    }

    void add_weak() {
        weak_count++;
    }

    size_t use_count() const noexcept {
        return ref_count;
    }

    size_t weak_use_count() const noexcept {
        return weak_count;
    }

    virtual ~control_block() = default;
    virtual void delete_object() = 0;
private:
    size_t ref_count = 1;
    size_t weak_count = 0;
};

template <typename Y, typename D = std::default_delete<Y>>
struct control_block_ptr : control_block, D {
    control_block_ptr(Y* p, D d = std::default_delete<Y>()) : D(std::move(d)), ptr(p) {}
    
    void delete_object() noexcept override {
        static_cast<D&>(*this)(ptr); // кастуемся к типу D и вызываем делитер
    }

    ~control_block_ptr() override = default;
private:    
    Y* ptr;
};

template <typename Y, typename D = std::default_delete<Y>>
struct control_block_obj : control_block, D {
    
    template<typename... Args>
    control_block_obj(Args&&... args) {
        new (&data) Y(std::forward<Args>(args)...);
    }
    
    void delete_object() noexcept override {
        reinterpret_cast<Y*>(&data)->~Y();
    }

    Y* get() {
        return reinterpret_cast<Y*>(&data);
    }

    ~control_block_obj() override = default;

private: 
    typename std::aligned_storage<sizeof(Y), alignof(Y)>::type data;
};

template<typename T>
struct weak_ptr;

template <typename T>
struct shared_ptr {
    template<typename F>
    friend struct weak_ptr;

    template<typename F>
    friend struct shared_ptr;

    constexpr shared_ptr() noexcept = default;   

    template<typename Y>
    shared_ptr(shared_ptr<Y> const& rhs) noexcept : cb(rhs.cb), ptr(rhs.ptr) {
        if (cb != nullptr) {
            cb->add_ref();
        }
    }

    shared_ptr(shared_ptr const& rhs) noexcept : cb(rhs.cb), ptr(rhs.ptr) {
        if (cb != nullptr) {
            cb->add_ref();
        }
    }

    template<typename Y, typename D = std::default_delete<Y>>
    explicit shared_ptr(Y* p, D deleter = D{}) {
        try { 
            cb = new control_block_ptr(p, deleter); 
            ptr = p;
        } catch (...) {
            deleter(p);
            throw;
        }
    }
    
    template<typename Y>
    shared_ptr(shared_ptr<Y> const& r, T* ptr) noexcept : cb(r.cb), ptr(ptr) {
        if (cb != nullptr) {
            cb->add_ref();
        }
    }

    shared_ptr(std::nullptr_t) noexcept {};


    shared_ptr(shared_ptr&& rhs)  noexcept : cb(rhs.cb), ptr(rhs.ptr) {
        rhs.cb = nullptr;
        rhs.ptr = nullptr;
    }

    template<typename Y>
    shared_ptr(weak_ptr<Y> const& r) : cb(r.cb), ptr(r.ptr) {
        if (cb != nullptr) {
            cb->add_ref();
        }
    }

    shared_ptr<T>& operator=(shared_ptr const& rhs) noexcept {
        shared_ptr(rhs).swap(*this);
        return *this;
    }

    shared_ptr<T>& operator=(shared_ptr&& rhs) noexcept {
        if (this != &rhs) {
            shared_ptr(std::move(rhs)).swap(*this);
        }
        return *this;
    }


    T& operator*() const noexcept {
        return *ptr;
    }

    T* operator->() const noexcept {
        return ptr;
    }

    explicit operator bool() const noexcept {
        return ptr != nullptr;
    }

    T* get() const noexcept {
        return ptr;
    }

    size_t use_count() const noexcept {
        return cb != nullptr ? cb->use_count() : 0; 
    }

    void reset() noexcept {
        shared_ptr().swap(*this);
    }

    template<typename Y, typename Deleter = std::default_delete<Y>>
    void reset(Y* ptr, Deleter d = Deleter{}) {
        shared_ptr(ptr, d).swap(*this);
    }


    void swap(shared_ptr& r) noexcept {
        std::swap(this->ptr, r.ptr);
        std::swap(this->cb, r.cb);
    }

    ~shared_ptr() {
        if (cb == nullptr) {
            return;
        }
        cb->release_ref();
        if (cb->use_count() == 0 && cb->weak_use_count() == 0) {
            delete cb;
        }  
    }

    template<class A, typename... Args>
    friend shared_ptr<A> make_shared(Args&& ... args);

private:
    control_block* cb{};
    T* ptr{};
};


template<typename L, typename R>
bool operator==(shared_ptr<L> const& lhs, shared_ptr<R> const& rhs) {
    return lhs.get() == rhs.get();
}

template<typename L, typename R>
bool operator!=(shared_ptr<L> const& lhs, shared_ptr<R> const& rhs) {
       return lhs.get() != rhs.get();
    }

template<typename L>
bool operator==(shared_ptr<L> const& lhs, std::nullptr_t) {
    return lhs.get() == nullptr;
}

template<typename R>
bool operator==(std::nullptr_t, shared_ptr<R> const& lhs) {
    return lhs.get() == nullptr;
}

template<typename L>
bool operator!=(shared_ptr<L> const& lhs, std::nullptr_t) {
    return lhs.get() != nullptr;
}

template<typename R>
bool operator!=(std::nullptr_t, shared_ptr<R> const& lhs) {
    return lhs.get() != nullptr;
}


template<typename A, typename... Args>
shared_ptr<A> make_shared(Args&& ... args) {
    auto* bl = new control_block_obj<A>(std::forward<Args>(args)...);
    shared_ptr<A> cur;
    cur.cb = bl;
    cur.ptr = bl->get();
    return cur;
}

template<typename T>
struct weak_ptr {
    template<typename F>
    friend struct shared_ptr;

    constexpr weak_ptr() noexcept = default;

    weak_ptr(weak_ptr const& r) noexcept : cb(r.cb), ptr(r.ptr) {
        if (cb != nullptr) {
            cb->add_weak();
        }
    }

    weak_ptr(weak_ptr&& r) noexcept : cb(r.cb), ptr(r.ptr) {
        r.cb = nullptr;
        r.ptr = nullptr;
    }

    weak_ptr<T>& operator=(weak_ptr const& rhs) noexcept {
        weak_ptr(rhs).swap(*this);
        return *this;
    }

    weak_ptr<T>& operator=(weak_ptr&& rhs) noexcept {
        if (this != &rhs) {
            weak_ptr(std::move(rhs)).swap(*this);
        }
        return *this;
    }

    template<typename Y>
    weak_ptr(shared_ptr<Y> const& r) noexcept : cb(r.cb), ptr(r.ptr) {
        if (cb != nullptr) {
            cb->add_weak();
        }
    }

    size_t use_count() const noexcept {
        return cb == nullptr ? 0 : cb->use_count();
    }

    bool expired() const noexcept {
        return use_count() == 0;
    }

    shared_ptr<T> lock() const noexcept {
        return !expired() ? shared_ptr<T>(*this) : shared_ptr<T>();
    }

    void swap(weak_ptr& rhs) noexcept {
        std::swap(cb, rhs.cb);
        std::swap(ptr, rhs.ptr);
    }


    ~weak_ptr() noexcept {
        if (cb == nullptr) {
            return;
        }

        cb->release_weak();
        if (cb->use_count() == 0 && cb->weak_use_count() == 0) {
            delete cb;
        }
    }

private:
    control_block* cb{};
    T* ptr{};
};
