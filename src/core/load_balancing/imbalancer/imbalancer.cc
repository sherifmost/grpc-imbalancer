// Skeleton implementation of a custom LB policy named "imbalancer".
//
// This is a minimal scaffold that delegates to an internal child policy
// (default: round_robin). It is intended as a starting point for integrating
// custom coordinator-driven logic into gRPC's internal LB stack.

#include "src/core/load_balancing/lb_policy.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/core/config/core_configuration.h"
#include "src/core/load_balancing/delegating_helper.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kImbalancerPolicyName = "imbalancer";

class ImbalancerConfig final : public LoadBalancingPolicy::Config {
 public:
  ImbalancerConfig(std::string child_policy_name,
                   RefCountedPtr<LoadBalancingPolicy::Config> child_config)
      : child_policy_name_(std::move(child_policy_name)),
        child_config_(std::move(child_config)) {}

  absl::string_view name() const override { return kImbalancerPolicyName; }

  const std::string& child_policy_name() const { return child_policy_name_; }
  RefCountedPtr<LoadBalancingPolicy::Config> child_config() const {
    return child_config_;
  }

 private:
  std::string child_policy_name_;
  RefCountedPtr<LoadBalancingPolicy::Config> child_config_;
};

class ImbalancerLb final : public LoadBalancingPolicy {
 public:
  explicit ImbalancerLb(Args args) : LoadBalancingPolicy(std::move(args)) {}

  absl::string_view name() const override { return kImbalancerPolicyName; }

  absl::Status UpdateLocked(UpdateArgs args) override {
    if (shutting_down_) return absl::OkStatus();
    auto* config = static_cast<ImbalancerConfig*>(args.config.get());
    if (config == nullptr) {
      return absl::InvalidArgumentError("imbalancer: missing config");
    }
    // Create or replace child policy if needed.
    if (child_policy_ == nullptr ||
        child_policy_name_ != config->child_policy_name()) {
      child_policy_name_ = config->child_policy_name();
      child_policy_ = CreateChildPolicy(child_policy_name_, args.args);
      if (child_policy_ == nullptr) {
        return absl::InvalidArgumentError("imbalancer: failed to create child policy");
      }
    }
    // Forward update to child policy using its config.
    UpdateArgs child_args = std::move(args);
    child_args.config = config->child_config();
    return child_policy_->UpdateLocked(std::move(child_args));
  }

  void ExitIdleLocked() override {
    if (child_policy_ != nullptr) child_policy_->ExitIdleLocked();
  }

  void ResetBackoffLocked() override {
    if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
  }

 private:
  class Helper final
      : public LoadBalancingPolicy::ParentOwningDelegatingChannelControlHelper<
            ImbalancerLb> {
   public:
    explicit Helper(RefCountedPtr<ImbalancerLb> parent)
        : ParentOwningDelegatingChannelControlHelper(std::move(parent)) {}
  };

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicy(
      absl::string_view name, const ChannelArgs& args) {
    LoadBalancingPolicy::Args lb_args;
    lb_args.work_serializer = work_serializer();
    lb_args.args = args;
    lb_args.channel_control_helper =
        std::make_unique<Helper>(Ref(DEBUG_LOCATION, "ImbalancerLbHelper"));
    return CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
        name, std::move(lb_args));
  }

  void ShutdownLocked() override {
    shutting_down_ = true;
    child_policy_.reset();
  }

  bool shutting_down_ = false;
  std::string child_policy_name_;
  OrphanablePtr<LoadBalancingPolicy> child_policy_;
};

class ImbalancerFactory final : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<ImbalancerLb>(std::move(args));
  }

  absl::string_view name() const override { return kImbalancerPolicyName; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    std::string child_policy = "round_robin";
    Json child_config = Json::FromObject({});
    if (json.type() == Json::Type::kObject) {
      auto it = json.object().find("childPolicy");
      if (it != json.object().end() && it->second.type() == Json::Type::kString) {
        child_policy = std::string(it->second.string());
      }
      auto it_cfg = json.object().find("childPolicyConfig");
      if (it_cfg != json.object().end() && it_cfg->second.type() == Json::Type::kObject) {
        child_config = it_cfg->second;
      }
    }

    // Build a LoadBalancingConfig array for parsing child policy config.
    Json::Array lb_config;
    lb_config.emplace_back(Json::FromObject({{child_policy, child_config}}));
    auto child_config_or =
        CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
            Json::FromArray(std::move(lb_config)));
    if (!child_config_or.ok()) return child_config_or.status();
    return MakeRefCounted<ImbalancerConfig>(child_policy,
                                            std::move(*child_config_or));
  }
};

}  // namespace

void RegisterImbalancerLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<ImbalancerFactory>());
}

}  // namespace grpc_core
