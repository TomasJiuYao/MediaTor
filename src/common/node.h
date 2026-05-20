#pragma once

#include <string>
#include <atomic>

class NodeBase {
public:
    virtual ~NodeBase() = default;

    virtual std::string name() const = 0;
    virtual void init() = 0;
    virtual void run()  = 0;
    virtual void stop() { running_.store(false); }

    void set_running(bool v) { running_.store(v); }
    bool is_running() const { return running_.load(); }

protected:
    std::atomic<bool> running_{false};
};
