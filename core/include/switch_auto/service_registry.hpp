#pragma once

#include <cstddef>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>

namespace switch_auto {

class ServiceRegistry {
public:
    template <typename Service>
    bool registerService(Service& service) {
        return services_.emplace(std::type_index(typeid(Service)), &service).second;
    }

    template <typename Service>
    Service* find() const {
        const auto found = services_.find(std::type_index(typeid(Service)));
        return found == services_.end() ? nullptr : static_cast<Service*>(found->second);
    }

    template <typename Service>
    Service& require() const {
        Service* service = find<Service>();
        if (!service) throw std::logic_error("required service is not registered");
        return *service;
    }

    std::size_t size() const { return services_.size(); }

private:
    std::unordered_map<std::type_index, void*> services_;
};

}  // namespace switch_auto
