#include "MapBasedGlobalLockImpl.h"
#include <iostream>
#include <mutex>

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::LRU(size_t how_many) {
    std::lock_guard<std::recursive_mutex> lock(mutex); 
    while (how_many + _list.size > _max_size) {
        _backend.erase(_list.tail->key);
        if (!_list.remove_last()) return false;
    }
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Put(const std::string &key, const std::string &value) { 
    if (key.size() + value.size() > _max_size) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto iter = _backend.find(std::reference_wrapper<const std::string>(key));  
    if (iter == _backend.end()) {
        size_t new_size = _list.size + key.size() + value.size();
        if (new_size > _max_size) {
            if (!LRU(key.size() + value.size())) {
                return false;
            }
        }
        auto elem = _list.push_front(key, value);
        std::reference_wrapper<const std::string> _key(elem->key);
        _backend.insert({_key, elem});
        
    } else {
        size_t new_size = _list.size - iter->second->value.size() + value.size();
        _list.move_front(iter->second);
        if (new_size > _max_size) { 
            _list.size -= iter->second->value.size();
            if(!LRU(value.size())) {
                return false;
            }
        }
        iter->second->value = value; 
    }
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::PutIfAbsent(const std::string &key, const std::string &value) { 
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto iter = _backend.find(key);  
    if (iter == _backend.end()) {
        return Put(key, value); 
    } 
    _list.move_front(iter->second);
    return false;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Set(const std::string &key, const std::string &value) { 
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto iter = _backend.find(key);
    if (iter != _backend.end()) {
        return Put(key, value);
    }
    return false;
}

// See MapBasedG:w
// lobalLockImpl.h
bool MapBasedGlobalLockImpl::Delete(const std::string &key) { 
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto iter = _backend.find(key);
    if (iter != _backend.end()) {
        auto p = iter->second;
        _backend.erase(key);
        return _list.remove(p);
    }
    return false;

}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Get(const std::string &key, std::string &value) const { 
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto iter = _backend.find(key);
    if (iter != _backend.end()) {
        _list.move_front(iter->second);        
        value = iter->second->value;
        return true;
    }
    return false;


}
} // namespace Backend
} // namespace Afina
