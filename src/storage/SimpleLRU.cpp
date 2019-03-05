#include "SimpleLRU.h"
#include <iostream>

namespace Afina {
namespace Backend {

bool SimpleLRU::_ReduceToSize(std::size_t size) {
    while(_size > size) {
        _DeleteFromTail();
    }
    return true;
}

bool SimpleLRU::_DeleteFromTail() {
    if(_lru_index.empty()) {
        return false;
    }
    lru_node* tmp = _lru_tail;
    std::size_t del_size = tmp->key.size() + tmp->value.size();
    lru_node* new_tail = tmp->prev;
    const std::string &del_key = tmp->key;
    auto found = _lru_index.find(del_key);
    if(del_key == _lru_head->key) {
        delete _lru_head.release();
    } else {
        delete tmp->prev->next.release();
    }
    _lru_index.erase(found);
    _size -= del_size;
    _lru_tail = new_tail;
    if(_lru_tail != nullptr) {
        _lru_tail->next = nullptr;
    }
    return true;
}

bool SimpleLRU::_MoveToHead(std::reference_wrapper<lru_node> ref) {
    if(_lru_head.get() == &ref.get()) {
        return true;
    }
    if(_lru_tail == &ref.get()) {
        // our element is in tail, and tail != head
        _lru_head->prev = &ref.get();
        _lru_tail = ref.get().prev;
        ref.get().prev->next.swap(ref.get().next);
        ref.get().next.swap(_lru_head);
    } else {
        // our element is not in head or tail
        _lru_head->prev = &ref.get();
        ref.get().next->prev = ref.get().prev;
        ref.get().prev->next.swap(ref.get().next);
        ref.get().next.swap(_lru_head);
    }
    _lru_head->prev = nullptr;
    return true;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string &key, const std::string &value) {
    if(key.size() + value.size() > _max_size) {
        return false;
    }
    if(PutIfAbsent(key, value)) {
        return true;
    }
    std::reference_wrapper<lru_node> ref = _lru_index.find(key)->second;
    std::size_t size_inc = value.size() - ref.get().value.size();
    _ReduceToSize(_max_size - size_inc);
    _lru_head->value = value;
    _size += size_inc;
    return true;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value) {
    std::size_t entry_size = key.size() + value.size();
    if(entry_size > _max_size) {
        return false;
    }
    bool empty = _lru_index.empty();
    if(!empty) {
        if(_lru_index.find(key) != _lru_index.end()) {
            return false;
        }
    }
    lru_node* node = new lru_node{key, value, nullptr, nullptr};
    if(!empty) {
        _ReduceToSize(_max_size - entry_size);
        empty = _lru_index.empty();
    }
    if(!empty) {
        _lru_head->prev = node;
        _lru_head.swap(node->next);
    } else {
        _lru_tail = node;
    }
    _lru_head.reset(node);
    _lru_index.insert({std::cref(_lru_head->key), std::ref(*node)});
    _size += entry_size;

    return true;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Set(const std::string &key, const std::string &value) {
    if(key.size() + value.size() > _max_size) {
        return false;
    }
    if(_lru_index.empty()) {
        return false;
    }
    auto found = _lru_index.find(key);
    if(found == _lru_index.end()) {
        return false;
    }
    std::reference_wrapper<lru_node> ref = found->second;
    std::size_t size_inc = value.size() - ref.get().value.size();
    _MoveToHead(ref);
    _ReduceToSize(_max_size - size_inc);
    ref.get().value = value;
    _size += size_inc;
    return true;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Delete(const std::string &key) {
    if(_lru_index.empty()) {
        return false;
    }
    auto found = _lru_index.find(key);
    if(found == _lru_index.end()) {
        return false;
    }
    std::reference_wrapper<lru_node> ref = found->second;
    std::size_t del_size = key.size() + ref.get().value.size();
    lru_node* tmp = &ref.get();
    if(_lru_head.get() == tmp) {
        _lru_head.swap(tmp->next);
        _lru_head->prev = nullptr;
        if(_lru_head.get() == _lru_tail) {
            _lru_tail = nullptr;
        }
    } else if(_lru_tail == tmp) {
        tmp->prev->next.swap(tmp->next);
        _lru_tail = tmp->prev;
    } else {
        tmp->next->prev = tmp->prev;
        tmp->prev->next.swap(tmp->next);
    }
    delete tmp->next.release();
    _lru_index.erase(found);
    _size -= del_size;
    return true;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Get(const std::string &key, std::string &value) {
    if(_lru_index.empty()) {
        return false;
    }
    auto found = _lru_index.find(key);
    if(found == _lru_index.end()) {
        return false;
    }
    std::reference_wrapper<lru_node> ref = found->second;
    _MoveToHead(ref);
    value = ref.get().value;
    return true;
}

void SimpleLRU::_PrintDebug(std::ostream& os) {
    os << "\tindex:\n";
    for(auto& pair: _lru_index) {
        os << "(" << pair.first.get() << ")\n";
    }
    os << "\t</index>\n";
    lru_node* tmp = _lru_head.get();
    os << "\tLIST:" << "\n";
    while(tmp!=nullptr) {
        os << tmp->key << " " << tmp->value << " " <<
                     ((tmp->next!=nullptr) ? "_" : "0") << " " <<
                     ((tmp->prev!=nullptr) ? "_" : "0") << " //\n";
        tmp = tmp->next.get();
    }
    os << "  REVERSE:" << "\n";
    tmp = _lru_tail;
    while(tmp!=nullptr) {
        os << tmp->key << " " << tmp->value << " " <<
                     ((tmp->next!=nullptr) ? "_" : "0") << " " <<
                     ((tmp->prev!=nullptr) ? "_" : "0") << " \\\\\n";
        tmp = tmp->prev;
    }
    os << "\t</LIST>" << "\n";
}

} // namespace Backend
} // namespace Afina
