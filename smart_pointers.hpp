#include <iostream>
#include <memory>

template <typename T>
class SharedPtr {
 public:
  class BasePtrCounter {
   public:
    virtual T* get_ptr() const = 0;
    virtual void destroy() = 0;
    virtual void deallocate() = 0;

    void increment_shared_count() { ++shared_count_; }
    void decrement_shared_count() { --shared_count_; }
    void increment_weak_count() { ++weak_count_; }
    void decrement_weak_count() { --weak_count_; }

    uint32_t get_shared_count() const { return shared_count_; }
    uint32_t get_weak_count() const { return weak_count_; }

   private:
    uint32_t shared_count_ = 0;
    uint32_t weak_count_ = 0;
  };

  template <typename Y>
  class DirectPtrCounter : public BasePtrCounter {
   public:
    explicit DirectPtrCounter(Y* obj) : ptr_(obj) {}

    T* get_ptr() const override { return reinterpret_cast<T*>(ptr_); }

    void destroy() override {
      std::default_delete<Y> deleter_obj;
      deleter_obj(ptr_);
      ptr_ = nullptr;
    }

    void deallocate() override {
      std::allocator<Y> allocator_obj;

      typename std::allocator_traits<std::allocator<Y>>::template rebind_alloc<
          DirectPtrCounter<Y>>(allocator_obj)
          .deallocate(this, 1);
    }

   private:
    Y* ptr_;
  };

  class NonDirectPtrCounter : public BasePtrCounter {
   public:
    template <typename... Args>
    explicit NonDirectPtrCounter(Args&&... args)
        : ptr_obj_(std::forward<Args>(args)...) {}

    T* get_ptr() const override { return const_cast<T*>(&ptr_obj_); }

    void destroy() override {
      std::default_delete<T> deleter_obj;
      deleter_obj(get_ptr());
    }

    void deallocate() override {
      std::allocator<T> allocator_obj;

      typename std::allocator_traits<std::allocator<T>>::template rebind_alloc<
          NonDirectPtrCounter>(allocator_obj)
          .deallocate(this, 1);
    }

   private:
    T ptr_obj_;
  };

  SharedPtr(BasePtrCounter* ptr_counter);

  SharedPtr();

  template <typename Y>
  SharedPtr(Y* ptr);

  SharedPtr(const SharedPtr& other_ptr);

  SharedPtr(SharedPtr&& other_ptr);

  const SharedPtr& operator=(const SharedPtr& other_ptr);

  template <typename Y>
  const SharedPtr& operator=(const SharedPtr<Y>& other_ptr);

  const SharedPtr& operator=(SharedPtr&& other_ptr);

  T* get() const;

  BasePtrCounter* get_ptr_counter() const { return ptr_counter_; }

  T& operator*() const;

  T* operator->() const;

  uint32_t use_count() const;

  void reset();

  void reset_ptr() { this->ptr_ = nullptr; }

  void reset_ptr_counter() { this->ptr_counter_ = nullptr; }

  ~SharedPtr();

 private:
  BasePtrCounter* ptr_counter_;

  T* ptr_;
};

template <typename T>
SharedPtr<T>::SharedPtr() : ptr_counter_(nullptr), ptr_(nullptr) {}

template <typename T>
SharedPtr<T>::SharedPtr(BasePtrCounter* ptr_counter)
    : ptr_counter_(ptr_counter),
      ptr_(ptr_counter ? ptr_counter->get_ptr() : nullptr) {
  if (ptr_counter) {
    ptr_counter->increment_shared_count();
  }
}

template <typename T>
template <typename Y>
SharedPtr<T>::SharedPtr(Y* ptr) : ptr_(reinterpret_cast<T*>(ptr)) {
  typename std::allocator_traits<std::allocator<Y>>::template rebind_alloc<
      DirectPtrCounter<Y>>
      custom_allocator = std::allocator<Y>();

  BasePtrCounter* temp_ptr_counter = std::allocator_traits<
      typename std::allocator_traits<std::allocator<Y>>::template rebind_alloc<
          DirectPtrCounter<Y>>>::allocate(custom_allocator, 1);

  new (temp_ptr_counter) DirectPtrCounter<Y>(ptr);
  ptr_counter_ = temp_ptr_counter;
  if (ptr_counter_) {
    ptr_counter_->increment_shared_count();
  }
}

template <typename T>
SharedPtr<T>::SharedPtr(const SharedPtr& other_ptr)
    : ptr_counter_(other_ptr.get_ptr_counter()), ptr_(other_ptr.get()) {
  if (ptr_counter_) {
    ptr_counter_->increment_shared_count();
  }
}

template <typename T>
SharedPtr<T>::SharedPtr(SharedPtr&& other_ptr)
    : ptr_counter_(other_ptr.get_ptr_counter()), ptr_(other_ptr.get()) {
  other_ptr.reset_ptr_counter();
  other_ptr.reset_ptr();
}

template <typename T>
const SharedPtr<T>& SharedPtr<T>::operator=(const SharedPtr& other_ptr) {
  if (this != &other_ptr) {
    if (ptr_counter_) {
      this->~SharedPtr();
    }
    ptr_counter_ =
        reinterpret_cast<BasePtrCounter*>(other_ptr.get_ptr_counter());
    ptr_ = other_ptr.get();
    if (ptr_counter_) {
      ptr_counter_->increment_shared_count();
    }
  }
  return *this;
}

template <typename T>
template <typename Y>
const SharedPtr<T>& SharedPtr<T>::operator=(const SharedPtr<Y>& other_ptr) {
  if (this != reinterpret_cast<const void*>(&other_ptr)) {
    if (ptr_counter_) {
      this->~SharedPtr();
    }
    ptr_ = other_ptr.get();
    ptr_counter_ = dynamic_cast<BasePtrCounter*>(other_ptr.get_ptr_counter());
    if (ptr_counter_) {
      ptr_counter_->increment_shared_count();
    }
  }
  return *this;
}

template <typename T>
const SharedPtr<T>& SharedPtr<T>::operator=(SharedPtr&& other_ptr) {
  if (this != &other_ptr) {
    if (ptr_counter_) {
      this->~SharedPtr();
    }
    ptr_counter_ = std::move(
        reinterpret_cast<BasePtrCounter*>(other_ptr.get_ptr_counter()));
    ptr_ = std::move(other_ptr.get());
    other_ptr.reset_ptr();
    other_ptr.reset_ptr_counter();
  }
  return *this;
}

template <typename T>
T* SharedPtr<T>::get() const {
  if (ptr_) {
    return ptr_;
  }
  return ptr_counter_ ? ptr_counter_->get_ptr() : nullptr;
}

template <typename T>
T& SharedPtr<T>::operator*() const {
  return ptr_ ? *ptr_ : *(ptr_counter_->get_ptr());
}

template <typename T>
T* SharedPtr<T>::operator->() const {
  return this->get();
}

template <typename T>
uint32_t SharedPtr<T>::use_count() const {
  return ptr_counter_ ? ptr_counter_->get_shared_count() : 0;
}

template <typename T>
void SharedPtr<T>::reset() {
  *this = SharedPtr<T>();
}

template <typename T>
SharedPtr<T>::~SharedPtr() {
  if (ptr_counter_ && ptr_counter_->get_shared_count() > 0) {
    ptr_counter_->decrement_shared_count();
    if (ptr_counter_->get_shared_count() == 0) {
      ptr_counter_->destroy();
      if (ptr_counter_->get_weak_count() == 0) {
        ptr_counter_->deallocate();
      }
    }
  }
}

template <typename T>
class WeakPtr {
 public:
  WeakPtr();

  template <typename Y>
  WeakPtr(const WeakPtr<Y>& other_ptr);

  template <typename Y>
  WeakPtr(WeakPtr<Y>&& other_ptr);

  template <typename Y>
  WeakPtr(const SharedPtr<Y>& other_ptr);

  bool expired();

  typename SharedPtr<T>::BasePtrCounter* lock();

  template <typename Y>
  const WeakPtr<T>& operator=(const WeakPtr<Y>& other_ptr);

  template <typename Y>
  WeakPtr<T>& operator=(WeakPtr<Y>&& other_ptr);

  ~WeakPtr();

 private:
  template <typename Y>
  friend class WeakPtr;

  typename SharedPtr<T>::BasePtrCounter* ptr_counter_;
};

template <typename T>
WeakPtr<T>::WeakPtr() : ptr_counter_(nullptr) {}

template <typename T>
template <typename Y>
WeakPtr<T>::WeakPtr(const WeakPtr<Y>& other_ptr)
    : ptr_counter_(reinterpret_cast<typename SharedPtr<T>::BasePtrCounter*>(
          other_ptr.ptr_counter_)) {
  if (ptr_counter_) {
    ptr_counter_->increment_weak_count();
  }
}

template <typename T>
template <typename Y>
WeakPtr<T>::WeakPtr(WeakPtr<Y>&& other_ptr)
    : ptr_counter_(reinterpret_cast<typename SharedPtr<T>::BasePtrCounter*>(
          other_ptr.ptr_counter_)) {
  other_ptr.ptr_counter_ = nullptr;
}

template <typename T>
template <typename Y>
WeakPtr<T>::WeakPtr(const SharedPtr<Y>& other_ptr)
    : ptr_counter_(reinterpret_cast<typename SharedPtr<T>::BasePtrCounter*>(
          other_ptr.get_ptr_counter())) {
  ptr_counter_->increment_weak_count();
}

template <typename T>
bool WeakPtr<T>::expired() {
  return static_cast<bool>(!ptr_counter_ ||
                           !(ptr_counter_->get_shared_count()));
}

template <typename T>
typename SharedPtr<T>::BasePtrCounter* WeakPtr<T>::lock() {
  return expired()
             ? std::runtime_error("Попытка обратиться по устарелой ссылке")
             : ptr_counter_;
}

template <typename T>
template <typename Y>
const WeakPtr<T>& WeakPtr<T>::operator=(const WeakPtr<Y>& other_ptr) {
  if (this != &other_ptr) {
    reinterpret_cast<typename SharedPtr<T>::BasePtrCounter*>(
        other_ptr.get_ptr_counter())
        ->increment_weak_count();
    this->~WeakPtr();
  }
  return *this;
}

template <typename T>
template <typename Y>
WeakPtr<T>& WeakPtr<T>::operator=(WeakPtr<Y>&& other_ptr) {
  if (this != &other_ptr) {
    if (ptr_counter_ && ptr_counter_->get_weak_count() > 0) {
      ptr_counter_->decrement_weak_count();
      if (ptr_counter_->get_weak_count() == 0 &&
          ptr_counter_->get_shared_count() == 0) {
        ptr_counter_->deallocate();
      }
    }
    ptr_counter_ = other_ptr.get_ptr_counter();
    other_ptr.reset_ptr_counter();
  }
  return *this;
}

template <typename T>
WeakPtr<T>::~WeakPtr() {
  if (ptr_counter_) {
    if (ptr_counter_->get_weak_count() > 0) {
      ptr_counter_->decrement_weak_count();
    }
    if (ptr_counter_->get_weak_count() == 0 &&
        ptr_counter_->get_shared_count() == 0) {
      ptr_counter_->deallocate();
    }
  }
}

template <typename T, typename... Args>
SharedPtr<T> MakeShared(Args&&... args) {
  std::allocator<typename SharedPtr<T>::NonDirectPtrCounter> allocator;

  typename SharedPtr<T>::NonDirectPtrCounter* temp_ptr =
      std::allocator_traits<std::allocator<
          typename SharedPtr<T>::NonDirectPtrCounter>>::allocate(allocator, 1);

  std::allocator_traits<
      std::allocator<typename SharedPtr<T>::NonDirectPtrCounter>>::
      construct(allocator, temp_ptr, std::forward<Args>(args)...);

  SharedPtr<T> ptr(temp_ptr);
  return std::move(ptr);
}