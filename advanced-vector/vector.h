#pragma once
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;
    RawMemory(RawMemory&& other) noexcept
        :buffer_(std::exchange(other.buffer_, nullptr)), capacity_(std::exchange(other.capacity_, 0)){}
    RawMemory& operator=(RawMemory&& rhs) noexcept {
        if (this != &rhs) {
            buffer_ = std::exchange(rhs.buffer_, nullptr);
            capacity_ = std::exchange(rhs.capacity_, 0);
        }
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    Vector() = default;

    explicit Vector(size_t size)
        :data_(size), size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size_);
    }

    Vector(const Vector& other)
        :data_(other.size_), size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept
        :data_(std::move(other.data_)), size_(other.size_) {}

    ~Vector() {
        if (data_.GetAddress() != nullptr) {
            std::destroy_n(data_.GetAddress(), size_);
        }
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            }
            else {

                if (rhs.size_ < size_) {
                    for (size_t i = 0; i < rhs.size_; ++i) {
                        data_[i] = rhs.data_[i];
                    }
                    std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
                }
                else if (rhs.size_ > size_) {
                    for (size_t i = 0; i < size_; ++i) {
                        data_[i] = rhs.data_[i];
                    }
                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_, data_.GetAddress() + size_);
                }
            }
            size_ = rhs.size_;
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            data_ = std::move(rhs.data_);
            if (rhs.size_ < size_) {
                std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
            }
            size_ = rhs.size_;
        }
        return *this;
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data{ new_capacity };

        data_.Swap(ReallocateElements(new_data));
    }

    void Resize(size_t new_size) {
        if (new_size <= size_) {
            std::destroy_n(data_ + new_size, size_ - new_size);
        }
        else {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_ + size_, new_size - size_);
        }
        size_ = new_size;
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *(Emplace(end(), std::forward<Args>(args)...));
    }

    void PushBack(const T& value) {
        Emplace(end(), value);

    }
    void PushBack(T&& value) {
        Emplace(end(), std::move(value));
    }
    void PopBack() noexcept {
        --size_;
        std::destroy_at(data_ + size_);
    }

    // ======= iterators ========
    using iterator = T*;
    using const_iterator = const T*;

    iterator begin() noexcept {
        return data_.GetAddress();
    }
    iterator end() noexcept {
        return data_.GetAddress() + size_;
    }
    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }
    const_iterator end() const noexcept {
        return data_.GetAddress() + size_;
    }
    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }
    const_iterator cend() const noexcept {
        return data_.GetAddress() + size_;
    }
    // ==========================

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        size_t pos_index = pos - begin();
        if (size_ < Capacity()) {
            InsertWithoutAllocation(pos_index, std::forward<Args>(args)...);
        }
        else {
            InsertWithAllocation(pos_index, std::forward<Args>(args)...);
        }
        ++size_;
        return begin() + pos_index;;
    }
    iterator Erase(const_iterator pos) /*noexcept(std::is_nothrow_move_assignable_v<T>)*/ {
        iterator it = begin() + (pos - begin());
        std::move(it + 1, end(), it);
        std::destroy_at(end() - 1);
        --size_;
        return it;
    }
    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }
    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    void MoveOrCopyElements(T* from_address, size_t size, T* to_address) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(from_address, size, to_address);
        }
        else {
            std::uninitialized_copy_n(from_address, size, to_address);
        }
    }

    RawMemory<T>& ReallocateElements(RawMemory<T>& new_data) {
        MoveOrCopyElements(data_.GetAddress(), size_, new_data.GetAddress());
        std::destroy_n(data_.GetAddress(), size_);
        return new_data;
    }

    template <typename... Args>
    void InsertWithoutAllocation(size_t pos_index, Args&&... args) {
        if ((begin() + pos_index) != end()) {
            // create temp var
            T temp{ std::forward<Args>(args)... };
            // copy last elem to the end() pos
            MoveOrCopyElements(end() - 1, 1, end());
            std::move_backward(data_ + pos_index, end() - 1, end());
            data_[pos_index] = std::move(temp);
        }
        else {
            new (end()) T(std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void InsertWithAllocation(size_t pos_index, Args&&... args) {
        RawMemory<T> new_data{ size_ > 0 ? size_ * 2 : 1 };
        iterator it = begin() + pos_index;
        new (new_data.GetAddress() + pos_index) T(std::forward<Args>(args)...);
        try {
            MoveOrCopyElements(begin(), pos_index, new_data.GetAddress());
        }
        catch (...) {
            std::destroy_at(new_data.GetAddress() + pos_index);
        }
        try {
            MoveOrCopyElements(it, end() - it, new_data.GetAddress() + pos_index + 1);
        }
        catch (...) {
            std::destroy_n(new_data.GetAddress(), pos_index);
        }
        // destroy old data
        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }
};
