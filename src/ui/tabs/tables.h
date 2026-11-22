#pragma once
// Grid, Last, Drivers and Teams tabs, and the spoiler-free cover (port of ui/tabs/tables.py).

#include "common.h"

#include <functional>

namespace f1::ui {

class GridTab : public Tab {
    Q_OBJECT
public:
    explicit GridTab(Ctx* ctx, QWidget* parent = nullptr);
};

class ResultsTab : public Tab {
    Q_OBJECT
public:
    explicit ResultsTab(Ctx* ctx, QWidget* parent = nullptr);
};

class DriversTab : public Tab {
    Q_OBJECT
public:
    explicit DriversTab(Ctx* ctx, QWidget* parent = nullptr);
};

class ConstructorsTab : public Tab {
    Q_OBJECT
public:
    explicit ConstructorsTab(Ctx* ctx, QWidget* parent = nullptr);
};

// Shown instead of results, grid, live timing and standings in spoiler-free mode.
class SpoilerTab : public Tab {
    Q_OBJECT
public:
    SpoilerTab(Ctx* ctx, std::function<void()> reveal, QWidget* parent = nullptr);
};

}  // namespace f1::ui
