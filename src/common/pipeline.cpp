#include "pipeline.h"

#include <cstdio>

Pipeline::~Pipeline() {
    stop();
}

void Pipeline::add_node(std::shared_ptr<NodeBase> node) {
    nodes_.push_back(std::move(node));
}

void Pipeline::register_closer(std::function<void()> closer) {
    closers_.push_back(std::move(closer));
}

void Pipeline::start() {
    for (auto &node : nodes_) {
        node->set_running(true);
        threads_.emplace_back([&node]() {
            printf("[%s] thread started\n", node->name().c_str());
            try {
                node->run();
            } catch (const std::exception &e) {
                fprintf(stderr, "[%s] exception: %s\n", node->name().c_str(), e.what());
            }
            printf("[%s] thread stopped\n", node->name().c_str());
        });
    }
}

void Pipeline::wait() {
    for (auto &t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
}

void Pipeline::stop() {
    /* Signal all nodes to stop */
    for (auto &node : nodes_) {
        node->stop();
    }

    /* Close all queues so blocked threads can unblock */
    for (auto &closer : closers_) {
        closer();
    }
}
