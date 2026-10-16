// The plugin's own FM1 Open as one end of a sync (firmwares/fm1open/Fm1OpenSync.h): its transfer
// protocol served at once by its engine, as an FM-1 running it would serve it over USB MIDI.
#pragma once

#include <memory>

#include "Fm1OpenEngine.h"
#include "Fm1OpenSync.h"

namespace fm1open {

class EngineEndpoint : public Endpoint {
public:
    explicit EngineEndpoint(std::shared_ptr<Fm1OpenEngine> e) : e_(std::move(e)) {}
    std::optional<Bytes> ask(const Bytes& request) override {
        const auto q = parse(request);
        if (!q || !e_) return std::nullopt;
        for (auto& m : e_->request(request))
            if (auto r = parse(m); r && r->command == (q->command | 64) && r->token == q->token) return m;
        return std::nullopt;
    }
private:
    std::shared_ptr<Fm1OpenEngine> e_;
};

}  // namespace fm1open
