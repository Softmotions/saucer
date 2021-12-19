#include "utils.win32.hpp"
#include <Windows.h>
#include <dwmapi.h>
#include <system_error>
#include <wil/win32_helpers.h>
#include <window.hpp>

namespace saucer
{
    struct window::impl
    {
        HWND hwnd;
        std::size_t max_width, max_height;
        std::size_t min_width, min_height;

        static HINSTANCE instance;
        static WNDCLASSW wnd_class;
        static std::atomic<int> open_windows;
        static LRESULT wnd_proc(HWND, UINT, WPARAM, LPARAM);
    };

    WNDCLASSW window::impl::wnd_class;
    HINSTANCE window::impl::instance;
    std::atomic<int> window::impl::open_windows;

    LRESULT window::impl::wnd_proc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param)
    {
        const auto *thiz = reinterpret_cast<window *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (thiz)
        {
            switch (msg)
            {
            case WM_GETMINMAXINFO: {
                auto *info = reinterpret_cast<MINMAXINFO *>(l_param);
                if (thiz->m_impl->max_height && thiz->m_impl->max_width)
                {
                    info->ptMaxTrackSize.x = static_cast<int>(thiz->m_impl->max_width);
                    info->ptMaxTrackSize.y = static_cast<int>(thiz->m_impl->max_height);
                }
                if (thiz->m_impl->min_height && thiz->m_impl->min_width)
                {
                    info->ptMinTrackSize.x = static_cast<int>(thiz->m_impl->min_width);
                    info->ptMinTrackSize.y = static_cast<int>(thiz->m_impl->min_height);
                }
            }
            break;
            case WM_SIZE:
                if (thiz->m_resize_callback)
                {
                    std::size_t width = LOWORD(l_param);
                    std::size_t height = HIWORD(l_param);

                    thiz->m_resize_callback(width, height);
                }
                break;
            case WM_CLOSE:
                if (thiz->m_close_callback)
                {
                    if (thiz->m_close_callback())
                    {
                        return 0;
                    }
                }
                open_windows--;
                if (!open_windows)
                {
                    PostQuitMessage(0);
                }
                break;
            }
        }

        return DefWindowProcW(hwnd, msg, w_param, l_param);
    }

    window::~window() = default;
    window::window() : m_impl(std::make_unique<impl>())
    {
        if (!m_impl->instance)
        {
            m_impl->instance = GetModuleHandleW(nullptr);

            m_impl->wnd_class.lpszClassName = L"Saucer";
            m_impl->wnd_class.hInstance = m_impl->instance;
            m_impl->wnd_class.lpfnWndProc = m_impl->wnd_proc;

            if (!RegisterClassW(&m_impl->wnd_class))
            {
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
            }
        }

        m_impl->hwnd = CreateWindowExW(0, L"Saucer", L"Saucer Window", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                       nullptr, nullptr, m_impl->instance, nullptr);

        if (!m_impl->hwnd)
        {
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
        }

        auto *shcore = LoadLibraryW(L"Shcore.dll");
        auto set_process_dpi_awareness = reinterpret_cast<HRESULT (*)(DWORD)>(GetProcAddress(shcore, "SetProcessDpiAwareness"));

        if (set_process_dpi_awareness)
        {
            set_process_dpi_awareness(2);
        }
        else
        {
            auto *user32 = GetModuleHandleW(L"user32.dll");
            auto set_process_dpi_aware = reinterpret_cast<bool (*)()>(GetProcAddress(user32, "SetProcessDPIAware"));
            if (set_process_dpi_aware)
            {
                set_process_dpi_aware();
            }
        }

        SetWindowLongPtrW(m_impl->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        impl::open_windows++;
    }

    void window::on_close(const close_callback_t &callback)
    {
        m_close_callback = callback;
    }

    void window::on_resize(const resize_callback_t &callback)
    {
        m_resize_callback = callback;
    }

    void window::set_resizeable(bool enabled)
    {
        auto current_style = GetWindowLongW(m_impl->hwnd, GWL_STYLE);
        if (enabled)
        {
            current_style |= (WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        }
        else
        {
            current_style &= ~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        }

        SetWindowLongW(m_impl->hwnd, GWL_STYLE, current_style);
    }

    void window::set_decorations(bool enabled)
    {
        auto current_style = GetWindowLongW(m_impl->hwnd, GWL_STYLE);
        if (enabled)
        {
            current_style |= (WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
        }
        else
        {
            current_style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
        }

        SetWindowLongW(m_impl->hwnd, GWL_STYLE, current_style);
    }

    void window::set_title(const std::string &title)
    {
        SetWindowTextW(m_impl->hwnd, utils::widen(title).c_str());
    }

    void window::set_always_on_top(bool enabled)
    {
        // NOLINTNEXTLINE
        SetWindowPos(m_impl->hwnd, enabled ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    void window::set_size(std::size_t width, std::size_t height)
    {
        SetWindowPos(m_impl->hwnd, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height), SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }

    void window::set_max_size(std::size_t width, std::size_t height)
    {
        m_impl->max_width = width;
        m_impl->max_height = height;
    }

    void window::set_min_size(std::size_t width, std::size_t height)
    {
        m_impl->min_width = width;
        m_impl->min_height = height;
    }

    void window::run()
    {
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    void window::hide()
    {
        ShowWindow(m_impl->hwnd, SW_HIDE);
    }

    void window::show()
    {
        ShowWindow(m_impl->hwnd, SW_SHOW);
    }

    std::pair<std::size_t, std::size_t> window::get_size() const
    {
        RECT rect;
        GetClientRect(m_impl->hwnd, &rect);

        return std::make_pair(rect.right - rect.left, rect.bottom - rect.top);
    }

    std::pair<std::size_t, std::size_t> window::get_min_size() const
    {
        return std::make_pair(m_impl->min_width, m_impl->min_height);
    }

    std::pair<std::size_t, std::size_t> window::get_max_size() const
    {
        return std::make_pair(m_impl->max_width, m_impl->max_height);
    }

    bool window::get_always_on_top() const
    {
        return GetWindowLongW(m_impl->hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST;
    }

    std::string window::get_title() const
    {
        WCHAR title[256];
        GetWindowTextW(m_impl->hwnd, title, 256);

        return utils::narrow(title);
    }

    bool window::get_decorations() const
    {
        return GetWindowLongW(m_impl->hwnd, GWL_STYLE) & (WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    }

    bool window::get_resizeable() const
    {
        return GetWindowLongW(m_impl->hwnd, GWL_STYLE) & (WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    }

    void window::exit()
    {
        auto old_callback = m_close_callback;
        m_close_callback = nullptr;

        DestroyWindow(m_impl->hwnd);
        m_close_callback = old_callback;
    }
} // namespace saucer