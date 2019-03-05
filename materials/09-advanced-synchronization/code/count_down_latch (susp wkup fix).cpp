#include "count_down_latch.h"

countdownlatch::countdownlatch(uint32_t count) { this->count = count; }

void countdownlatch::await(uint64_t nanosecs) {
    std::unique_lock<std::mutex> lck(lock);
    if (0 == count) {
        return;
    } //открыть cppreference и прочитать про wait_for. в wait/wait_for можно сунуть ф-ю в кач-ве арг-та
    if (nanosecs > 0) { //есть еще wait_until - ему дают абс точку во времени (now+nanosecs), но этот метод немножко дороже
        cv.wait_for(lck, std::chrono::nanoseconds(nanosecs));
    } else {
        cv.wait(lck); //тут есть баг - не обрабатывается suspicious wakeup (может проснуца? а должен спать вечно. надо тут пилить цикл while)
    } // цикл нужен и для секции с nanosecs>0 (вроде), но там нужно учесть нужный таймаут шоб проспать сколько надо
} // в каком то смысле эта реал-я кд-латча может работать как барьер (у барьера тоже есть счетчик, когда 0 - разбудит всех (?))

uint32_t countdownlatch::get_count() {
    std::unique_lock<std::mutex> lck(lock);
    return count;
}

void countdownlatch::count_down() {
    std::unique_lock<std::mutex> lck(lock);
    if (0 == count) {
        return;
    }
    --count;
    if (0 == count) {
        cv.notify_all();
    }
}
