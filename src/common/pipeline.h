#pragma once

#include "node.h"

#include <memory>
#include <vector>
#include <thread>
#include <functional>

class Pipeline {
public:
    Pipeline() = default;
    ~Pipeline();

    Pipeline(const Pipeline &) = delete;
    Pipeline &operator=(const Pipeline &) = delete;

    /* Add a node to the pipeline */
    void add_node(std::shared_ptr<NodeBase> node);

    /* Register a queue's close function so Pipeline::stop() can close all queues */
    void register_closer(std::function<void()> closer);

    /* Initialize all nodes and start their threads */
    void start();

    /* Wait for all threads to finish */
    void wait();

    /* Signal all nodes to stop and close all queues */
    void stop();

private:
    std::vector<std::shared_ptr<NodeBase>> nodes_;
    std::vector<std::function<void()>>     closers_;
    std::vector<std::thread>               threads_;
};
