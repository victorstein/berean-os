#pragma once

#include <memory>
#include <span>
#include <type_traits>

#include "MappedInputManager.h"

class ButtonNavigator final {
  // Non-owning, non-allocating reference to a callable. Safe because every on*()
  // invokes it synchronously and never stores it, so a temporary lambda at the
  // call site outlives every use.
  class Callback {
    void* callable;
    void (*invoke)(void*);

   public:
    template <typename F>
      requires std::is_invocable_v<F&> && (!std::is_same_v<std::remove_cvref_t<F>, Callback>)
    // cppcheck-suppress noExplicitConstructor
    Callback(F&& f)
        : callable(const_cast<void*>(static_cast<const void*>(std::addressof(f)))),
          invoke([](void* target) { (*static_cast<std::remove_reference_t<F>*>(target))(); }) {}

    void operator()() const { invoke(callable); }
  };

  using Buttons = std::span<const MappedInputManager::Button>;

  static constexpr MappedInputManager::Button NEXT_BUTTONS[] = {MappedInputManager::Button::NavNext};
  static constexpr MappedInputManager::Button PREVIOUS_BUTTONS[] = {MappedInputManager::Button::NavPrevious};

  const uint16_t continuousStartMs;
  const uint16_t continuousIntervalMs;
  uint32_t lastContinuousNavTime = 0;
  static const MappedInputManager* mappedInput;

  [[nodiscard]] bool shouldNavigateContinuously() const;

 public:
  explicit ButtonNavigator(const uint16_t continuousIntervalMs = 500, const uint16_t continuousStartMs = 500)
      : continuousStartMs(continuousStartMs), continuousIntervalMs(continuousIntervalMs) {}

  static void setMappedInputManager(const MappedInputManager& mappedInputManager) { mappedInput = &mappedInputManager; }

  void onNext(Callback callback);
  void onPrevious(Callback callback);
  void onPressAndContinuous(Buttons buttons, Callback callback);
  void onPressAndContinuous(MappedInputManager::Button button, Callback callback) {
    onPressAndContinuous(Buttons(&button, 1), callback);
  }

  void onNextPress(Callback callback);
  void onPreviousPress(Callback callback);
  void onPress(Buttons buttons, Callback callback);

  void onNextRelease(Callback callback);
  void onPreviousRelease(Callback callback);
  void onRelease(Buttons buttons, Callback callback);

  void onNextContinuous(Callback callback);
  void onPreviousContinuous(Callback callback);
  void onContinuous(Buttons buttons, Callback callback);

  [[nodiscard]] static int nextIndex(int currentIndex, int totalItems);
  [[nodiscard]] static int previousIndex(int currentIndex, int totalItems);

  [[nodiscard]] static int nextPageIndex(int currentIndex, int totalItems, int itemsPerPage);
  [[nodiscard]] static int previousPageIndex(int currentIndex, int totalItems, int itemsPerPage);

  // Navigation uses the logical NavNext / NavPrevious buttons; MappedInputManager::mapButton resolves
  // them to physical buttons and applies any orientation-based direction swap, so this stays settings-free.
  [[nodiscard]] static constexpr Buttons getNextButtons() { return NEXT_BUTTONS; }
  [[nodiscard]] static constexpr Buttons getPreviousButtons() { return PREVIOUS_BUTTONS; }
};
