// Minimal COM smart pointer (no WRL dependency, works with MSVC and MinGW).
#pragma once

#include <unknwn.h>

#include <cstddef>
#include <utility>

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) {}
    ~ComPtr() { reset(); }

    ComPtr(const ComPtr& o) : p_(o.p_) {
        if (p_) p_->AddRef();
    }
    ComPtr(ComPtr&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
    ComPtr& operator=(ComPtr o) noexcept {
        std::swap(p_, o.p_);
        return *this;
    }

    void reset() {
        if (p_) {
            T* p = std::exchange(p_, nullptr);
            p->Release();
        }
    }

    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }

    // For out-parameters: releases the current pointer first.
    T** put() {
        reset();
        return &p_;
    }
    void** putVoid() { return reinterpret_cast<void**>(put()); }

    template <class U>
    HRESULT as(ComPtr<U>& out) const {
        if (!p_) return E_POINTER;
        return p_->QueryInterface(__uuidof(U), out.putVoid());
    }

private:
    T* p_ = nullptr;
};
