#include "qt.webview.impl.hpp"

#include "instantiate.hpp"

#include "qt.icon.impl.hpp"
#include "qt.window.impl.hpp"

#include "scheme.hpp"
#include "requests.hpp"

#include <fmt/core.h>

#include <QWebEngineScriptCollection>

#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlScheme>

namespace saucer
{
    webview::webview(const options &options) : window(options), m_impl(std::make_unique<impl>())
    {
        static std::once_flag flag;
        std::call_once(flag, []() { register_scheme("saucer"); });

        m_impl->profile = std::make_unique<QWebEngineProfile>("saucer");

        if (!options.storage_path.empty())
        {
            const auto path = QString::fromStdString(options.storage_path.string());

            m_impl->profile->setCachePath(path);
            m_impl->profile->setPersistentStoragePath(path);
        }

        m_impl->profile->setPersistentCookiesPolicy(options.persistent_cookies
                                                        ? QWebEngineProfile::ForcePersistentCookies
                                                        : QWebEngineProfile::NoPersistentCookies);

        m_impl->profile->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);

        m_impl->web_view    = std::make_unique<QWebEngineView>();
        m_impl->web_page    = std::make_unique<QWebEnginePage>(m_impl->profile.get());
        m_impl->channel     = std::make_unique<QWebChannel>();
        m_impl->channel_obj = std::make_unique<impl::web_class>(this);

        window::m_impl->window->setCentralWidget(m_impl->web_view.get());

        m_impl->web_view->setPage(m_impl->web_page.get());
        m_impl->web_page->setWebChannel(m_impl->channel.get());
        m_impl->channel->registerObject("saucer", m_impl->channel_obj.get());

        m_impl->web_view->connect(m_impl->web_view.get(), &QWebEngineView::loadStarted,
                                  [this]()
                                  {
                                      m_impl->dom_loaded = false;
                                      m_events.at<web_event::load_started>().fire();
                                  });

        window::m_impl->on_closed = [this]
        {
            set_dev_tools(false);
        };

        inject(impl::inject_script(), load_time::creation);
        inject(std::string{impl::ready_script}, load_time::ready);

        m_impl->web_view->show();
    }

    webview::~webview() = default;

    bool webview::on_message(const std::string &message)
    {
        if (message == "dom_loaded")
        {
            m_impl->dom_loaded = true;

            for (const auto &pending : m_impl->pending)
            {
                execute(pending);
            }

            m_impl->pending.clear();
            m_events.at<web_event::dom_ready>().fire();

            return true;
        }

        auto request = requests::parse(message);

        if (!request)
        {
            return false;
        }

        if (std::holds_alternative<requests::resize>(request.value()))
        {
            const auto data = std::get<requests::resize>(request.value());
            start_resize(static_cast<window_edge>(data.edge));

            return true;
        }

        if (std::holds_alternative<requests::drag>(request.value()))
        {
            start_drag();
            return true;
        }

        return false;
    }

    icon webview::favicon() const
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this] { return favicon(); }).get();
        }

        return {{m_impl->web_view->icon()}};
    }

    std::string webview::page_title() const
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this] { return page_title(); }).get();
        }

        return m_impl->web_view->title().toStdString();
    }

    bool webview::dev_tools() const
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this] { return dev_tools(); }).get();
        }

        return m_impl->dev_page != nullptr;
    }

    std::string webview::url() const
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this] { return url(); }).get();
        }

        return m_impl->web_view->url().toString().toStdString();
    }

    bool webview::context_menu() const
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this] { return context_menu(); }).get();
        }

        return m_impl->web_view->contextMenuPolicy() == Qt::ContextMenuPolicy::DefaultContextMenu;
    }

    color webview::background() const
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this] { return background(); }).get();
        }

        const auto color = m_impl->web_page->backgroundColor();

        return {
            static_cast<std::uint8_t>(color.red()),
            static_cast<std::uint8_t>(color.green()),
            static_cast<std::uint8_t>(color.blue()),
            static_cast<std::uint8_t>(color.alpha()),
        };
    }

    bool webview::force_dark_mode() const
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this] { return force_dark_mode(); }).get();
        }

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        const auto *settings = m_impl->profile->settings();
        return settings->testAttribute(QWebEngineSettings::ForceDarkMode);
#else
        return {};
#endif
    }

    void webview::set_dev_tools(bool enabled)
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this, enabled] { return set_dev_tools(enabled); }).get();
        }

        if (!m_impl->dev_page && !enabled)
        {
            return;
        }

        if (!enabled)
        {
            m_impl->web_page->setDevToolsPage(nullptr);
            m_impl->dev_page.reset();

            return;
        }

        if (!m_impl->dev_page)
        {
            m_impl->dev_page = std::make_unique<QWebEngineView>();
            m_impl->web_page->setDevToolsPage(m_impl->dev_page->page());
        }

        m_impl->dev_page->show();
    }

    void webview::set_context_menu(bool enabled)
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this, enabled] { return set_context_menu(enabled); }).get();
        }

        m_impl->web_view->setContextMenuPolicy(enabled ? Qt::ContextMenuPolicy::DefaultContextMenu
                                                       : Qt::ContextMenuPolicy::NoContextMenu);
    }

    void webview::set_background(const color &color)
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this, color] { return set_background(color); }).get();
        }

        const auto [r, g, b, a] = color;
        const auto transparent  = a < 255;

        m_impl->web_view->setAttribute(Qt::WA_TranslucentBackground, transparent);
        window::m_impl->set_alpha(transparent ? 0 : 255);

        m_impl->web_page->setBackgroundColor({r, g, b, a});
    }

    void webview::set_force_dark_mode(bool enabled)
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this, enabled] { return set_force_dark_mode(enabled); }).get();
        }

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        auto *settings = m_impl->profile->settings();
        settings->setAttribute(QWebEngineSettings::ForceDarkMode, enabled);
#endif
    }

    void webview::set_file(const fs::path &file)
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this, file] { return set_file(file); }).get();
        }

        m_impl->web_view->setUrl(QUrl::fromLocalFile(QString::fromStdString(file.string())));
    }

    void webview::set_url(const std::string &url)
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this, url] { return set_url(url); }).get();
        }

        m_impl->web_view->setUrl(QString::fromStdString(url));
    }

    void webview::serve(const std::string &file)
    {
        set_url(fmt::format("saucer://embedded/{}", file));
    }

    void webview::clear_scripts()
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this] { return clear_scripts(); }).get();
        }

        m_impl->web_view->page()->scripts().clear();

        inject(std::string{impl::ready_script}, load_time::ready);
        inject(impl::inject_script(), load_time::creation);
    }

    void webview::execute(const std::string &code)
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this, code] { execute(code); }).get();
        }

        if (!m_impl->dom_loaded)
        {
            m_impl->pending.emplace_back(code);
            return;
        }

        m_impl->web_view->page()->runJavaScript(QString::fromStdString(code));
    }

    void webview::handle_scheme(const std::string &name, scheme_handler handler)
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this, name, handler = std::move(handler)] mutable
                            { return handle_scheme(name, std::move(handler)); })
                .get();
        }

        if (m_impl->schemes.contains(name))
        {
            return;
        }

        auto &scheme_handler = m_impl->schemes.emplace(name, std::move(handler)).first->second;
        m_impl->web_view->page()->profile()->installUrlSchemeHandler(QByteArray::fromStdString(name), &scheme_handler);
    }

    void webview::remove_scheme(const std::string &name)
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this, name] { return remove_scheme(name); }).get();
        }

        const auto it = m_impl->schemes.find(name);

        if (it == m_impl->schemes.end())
        {
            return;
        }

        m_impl->web_view->page()->profile()->removeUrlSchemeHandler(&it->second);
        m_impl->schemes.erase(it);
    }

    void webview::clear(web_event event)
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this, event] { return clear(event); }).get();
        }

        m_events.clear(event);
    }

    void webview::remove(web_event event, std::uint64_t id)
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this, event, id] { return remove(event, id); }).get();
        }

        m_events.remove(event, id);
    }

    template <web_event Event>
    void webview::once(events::type<Event> callback)
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this, callback = std::move(callback)]() mutable
                            { return once<Event>(std::move(callback)); })
                .get();
        }

        m_impl->setup<Event>(this);
        m_events.at<Event>().once(std::move(callback));
    }

    template <web_event Event>
    std::uint64_t webview::on(events::type<Event> callback)
    {
        if (!window::m_impl->is_thread_safe())
        {
            return dispatch([this, callback = std::move(callback)]() mutable //
                            { return on<Event>(std::move(callback)); })
                .get();
        }

        m_impl->setup<Event>(this);
        return m_events.at<Event>().add(std::move(callback));
    }

    void webview::register_scheme(const std::string &name)
    {
        auto scheme = QWebEngineUrlScheme{name.c_str()};
        scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);

        using Flags = QWebEngineUrlScheme::Flag;
        scheme.setFlags(Flags::LocalScheme | Flags::LocalAccessAllowed | Flags::SecureScheme);

        QWebEngineUrlScheme::registerScheme(scheme);
    }

    INSTANTIATE_EVENTS(webview, 6, web_event)
} // namespace saucer
