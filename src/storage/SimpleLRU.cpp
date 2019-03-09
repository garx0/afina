#include "SimpleLRU.h"
#include <iostream>

namespace Afina {
namespace Backend {

void SimpleLRU::_Clear() {
    _lru_index.clear();
    delete _lru_head.release();
    _lru_tail = nullptr;
    _size = 0;
}

void SimpleLRU::_ReduceToSize(std::size_t size) {
    if(size == 0) {
        _Clear();
        return;
    }
    while(_size > size) {
        _DeleteFromTail();
    }
}

void SimpleLRU::_DeleteFromTail() {
    lru_node* tmp = _lru_tail;
    std::size_t del_size = tmp->key.size() + tmp->value.size();
    lru_node* new_tail = tmp->prev;
    const std::string &del_key = tmp->key;
    auto found = _lru_index.find(del_key);
    if(_lru_tail == _lru_head.get()) {
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
}

void SimpleLRU::_MoveToHead(std::reference_wrapper<lru_node> ref) {
    lru_node& node = ref.get();
    if(_lru_head.get() == &node) {
        return;
    }
    if(_lru_tail == &node) {
        // our element is in tail, and tail != head
        _lru_head->prev = &node;
        _lru_tail = node.prev;
        node.prev->next.swap(node.next);
        node.next.swap(_lru_head);
    } else {
        // our element is not in head or tail
        _lru_head->prev = &node;
        node.next->prev = node.prev;
        node.prev->next.swap(node.next);
        node.next.swap(_lru_head);
    }
    _lru_head->prev = nullptr;
}

bool SimpleLRU::_PutNew(const std::string &key, const std::string &value) {
    std::size_t entry_size = key.size() + value.size();
    if(entry_size > _max_size) {
        return false;
    }
    lru_node* node = new lru_node{key, value, nullptr, nullptr};
    _ReduceToSize(_max_size - entry_size);
    if(!_lru_index.empty()) {
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

bool SimpleLRU::_Set(std::reference_wrapper<lru_node> ref, const std::string &value) {
    std::size_t val_size = value.size();
    lru_node& node = ref.get();
    if(node.key.size() + val_size > _max_size) {
        return false;
    }
    std::size_t size_inc = val_size - node.value.size();
    _MoveToHead(ref);
    _ReduceToSize(_max_size - size_inc);
    node.value = value;
    _size += size_inc;
    return true;
}

void SimpleLRU::_Get(std::reference_wrapper<lru_node> ref, std::string &value) {
    _MoveToHead(ref);
    value = ref.get().value;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string &key, const std::string &value) {
    auto found = _lru_index.find(key);
    if(found != _lru_index.end()) {
        return _Set(found->second, value);
    }
    return _PutNew(key, value);
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value) {
    if(_lru_index.find(key) != _lru_index.end()) {
        return false;
    }
    return _PutNew(key, value);
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Set(const std::string &key, const std::string &value) {
    auto found = _lru_index.find(key);
    if(found == _lru_index.end()) {
        return false;
    }
    return _Set(found->second, value);
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Delete(const std::string &key) {
    auto found = _lru_index.find(key);
    if(found == _lru_index.end()) {
        return false;
    }
    std::reference_wrapper<lru_node> ref = found->second;
    lru_node& node = ref.get();
    std::size_t del_size = key.size() + node.value.size();
    lru_node* tmp = &node;
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
    auto found = _lru_index.find(key);
    if(found == _lru_index.end()) {
        return false;
    }
    _Get(found->second, value);
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
