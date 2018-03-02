#ifndef AFINA_STORAGE_MAP_BASED_GLOBAL_LOCK_IMPL_H
#define AFINA_STORAGE_MAP_BASED_GLOBAL_LOCK_IMPL_H

#include <unordered_map>
#include <mutex>
#include <string>
#include <functional>
#include <afina/Storage.h>

namespace Afina {
namespace Backend {

/**
 * # Map based implementation with global lock
 *
 *
 */

struct ListItem {
    std::string key;
    std::string value;
    ListItem *next = nullptr;
    ListItem *prev = nullptr;
    ListItem(const std::string & _key, const std::string & _value) :
    key{_key}, value{_value} 
    {}
};
struct List {
    ListItem *tail = nullptr;
    ListItem *head = nullptr;
    size_t size = 0;

    /*ListItem * push_back(std::string _key, std::string _value, std::mutex mutex) {
        ListItem *item = new Listitem(_key, _value);
        std::lock_guard<std::mutex> lock(mutex);
        if (tail == nullptr && head == nullptr) {
            tail = item;
            head = item;
        } else {
            tail->next = item;
            tail = item;
        }
        size += _key.size() + _value.size(); 
        return item;
    }*/

    //ListItem * push_front(std::string _key, std::string _value, std::mutex mutex) { 
    ListItem * push_front(const std::string & _key, const std::string & _value) { 
        ListItem *item = new ListItem(_key, _value);
        //std::lock_guard<std::mutex> lock(mutex);
        if (tail == nullptr && head == nullptr) {
            tail = item;
            head = item;
        } else {
            item->next = head;
            head->prev = item;
            head = item;
        }
        size += _key.size() + _value.size();
        return item;
    }
    
    //void move_front(const ListItem *list, std::mutex mutex) {
    void move_front(ListItem *list) {
        if (!list) return;
        if (head != list) {
            //std::lock_guard<std::mutex> lock(mutex);
            if (list != tail) { 
                list->next->prev = list->prev;
            } else {
                tail = tail->prev;
            }
            list->prev->next = list->next;
            list->next = head;
            head->prev = list;
            head = list;
        }
    }

    bool remove_last () {
        if (tail != nullptr) {
            //std::lock_guard<std::mutex> lock(mutex);
            if (tail != head) {
                tail->prev->next = nullptr;
            }
            auto t = tail;
            tail = t->prev;
            size -= t->value.size() + t->key.size(); 
            delete t; 
            return true;
        }
        return false;
    }

    bool remove(ListItem *item) {
        if (head != nullptr) {
            if (head != tail) {
                if (item == head) {
                    head = head->next;
                    head->prev = nullptr;
                } else if (item == tail) {
                    tail = tail->prev;
                    tail->next = nullptr;
                }
                else {
                    item->next->prev = item->prev;
                    item->prev->next = item->next;
                }
            } else {
                if (item != head) {
                    return false;
                }
                head = nullptr;
                tail = nullptr;
            }
            size -= item->key.size() + item->value.size();
            delete item;
            return true;
        }
        return false;
    }
};


class MapBasedGlobalLockImpl : public Afina::Storage {
public:
    MapBasedGlobalLockImpl(size_t max_size = 1024) : _max_size(max_size) {}
    ~MapBasedGlobalLockImpl() {}

    // Implements Afina::Storage interface
    bool Put(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool PutIfAbsent(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Set(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Delete(const std::string &key) override;

    // Implements Afina::Storage interface
    bool Get(const std::string &key, std::string &value) const override;

private:
    mutable std::recursive_mutex mutex;
    mutable List _list;
    size_t _max_size;
    std::unordered_map<std::reference_wrapper<const std::string>, ListItem *, std::hash<std::string>,
        std::equal_to<std::string>> _backend;
    bool LRU(size_t how_many);
};
} // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_MAP_BASED_GLOBAL_LOCK_IMPL_H
