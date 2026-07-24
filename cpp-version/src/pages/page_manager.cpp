#include "page_manager.h"

void PageManager::push(std::unique_ptr<Page> page) {
    pendingPush_.push_back(std::move(page));
}

void PageManager::pop() {
    pendingPops_++;
}

void PageManager::applyPending() {
    // Pops first (a page requesting pop then a new page push in the same frame ends
    // with the pushed page on top).
    while (pendingPops_ > 0 && !stack_.empty()) {
        stack_.back()->deactivate();
        stack_.pop_back();
        pendingPops_--;
        if (!stack_.empty()) stack_.back()->activate();
    }
    pendingPops_ = 0;

    for (auto& page : pendingPush_) {
        if (!stack_.empty()) stack_.back()->deactivate();
        stack_.push_back(std::move(page));
        stack_.back()->activate();
    }
    pendingPush_.clear();
}

void PageManager::update(float dt) {
    applyPending();
    if (!stack_.empty()) {
        stack_.back()->handleInput();
        stack_.back()->update(dt);
    }
}

void PageManager::render() {
    applyPending();
    if (!stack_.empty()) {
        stack_.back()->render();
    }
}
