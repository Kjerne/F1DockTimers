#include "widgets_button.h"

// UI Automation needs the OLE headers that WIN32_LEAN_AND_MEAN leaves out.
#undef WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <oleacc.h>
#include <UIAutomation.h>

#include <QLoggingCategory>
#include <chrono>

Q_LOGGING_CATEGORY(lcWidgets, "f1.widgets")

namespace f1 {

namespace {
constexpr auto POLL = std::chrono::milliseconds(5000);
}

WidgetsButtonWatch::~WidgetsButtonWatch() {
    stop();
    if (m_thread.joinable()) m_thread.join();
}

void WidgetsButtonWatch::start() {
    if (m_thread.joinable()) return;
    m_stop = false;
    m_thread = std::thread([this] { run(); });
}

void WidgetsButtonWatch::stop() { m_stop = true; }

std::optional<QRect> WidgetsButtonWatch::rect() const {
    std::lock_guard<std::mutex> g(m_lock);
    return m_rect;
}

void WidgetsButtonWatch::run() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
    IUIAutomation* uia = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                  reinterpret_cast<void**>(&uia));
    if (FAILED(hr) || !uia) {
        qCInfo(lcWidgets) << "UI Automation unavailable; Widgets button not tracked";
        CoUninitialize();
        return;
    }
    VARIANT var;
    VariantInit(&var);
    var.vt = VT_BSTR;
    var.bstrVal = SysAllocString(L"WidgetsButton");
    IUIAutomationCondition* cond = nullptr;
    if (SUCCEEDED(uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, var, &cond)) && cond) {
        while (!m_stop) {
            std::optional<QRect> found;
            if (HWND bar = FindWindowW(L"Shell_TrayWnd", nullptr)) {
                IUIAutomationElement* root = nullptr;
                IUIAutomationElement* btn = nullptr;
                RECT r{};
                if (SUCCEEDED(uia->ElementFromHandle(bar, &root)) && root &&
                    SUCCEEDED(root->FindFirst(TreeScope_Descendants, cond, &btn)) && btn &&
                    SUCCEEDED(btn->get_CurrentBoundingRectangle(&r)) && r.right > r.left && r.bottom > r.top)
                    found = QRect(QPoint(r.left, r.top), QPoint(r.right - 1, r.bottom - 1));
                if (btn) btn->Release();
                if (root) root->Release();
            }
            {
                std::lock_guard<std::mutex> g(m_lock);
                m_rect = found;
            }
            for (auto waited = std::chrono::milliseconds(0); waited < POLL && !m_stop; waited += std::chrono::milliseconds(100))
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        cond->Release();
    }
    VariantClear(&var);
    uia->Release();
    CoUninitialize();
}

std::optional<QRect> widgetsButtonRect() {
    static WidgetsButtonWatch* watch = nullptr;   // lives for the whole process
    if (!watch) {
        watch = new WidgetsButtonWatch();
        watch->start();
    }
    return watch->rect();
}

}  // namespace f1
