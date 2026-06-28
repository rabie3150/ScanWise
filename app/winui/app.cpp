#include "app.hpp"
#include "wic_helpers.h"

#include <Windows.h>
#undef GetCurrentTime
#include <commdlg.h>
#include <shellapi.h>
#include <shcore.h>
#include <objidl.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <MddBootstrap.h>
#include <microsoft.ui.xaml.window.h>

#include "scanwise/engine.hpp"

#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <cmath>

#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "comdlg32.lib")

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::System;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Controls::Primitives;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Microsoft::UI::Xaml::Media::Imaging;

namespace scanwise {

namespace {

const wchar_t* SCANWISE_TITLE = L"ScanWise";

Brush SolidBrush(uint8_t r, uint8_t g, uint8_t b) {
    return SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, r, g, b));
}

Brush DarkTextBrush() { return SolidBrush(32, 32, 32); }
Brush LightTextBrush() { return SolidBrush(255, 255, 255); }

bool IsImageFile(const std::wstring& path) {
    if (path.size() < 4) return false;
    auto ext = path.substr(path.find_last_of(L'.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext == L"png" || ext == L"jpg" || ext == L"jpeg" ||
           ext == L"bmp" || ext == L"tiff" || ext == L"tif" ||
           ext == L"webp" || ext == L"gif";
}

std::wstring OpenImageDialog() {
    wchar_t filename[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.webp\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return {};
    return filename;
}

std::wstring SaveImageDialog(bool jpeg) {
    wchar_t filename[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = jpeg ? L"JPEG Image\0*.jpg\0" : L"PNG Image\0*.png\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = jpeg ? L"jpg" : L"png";
    if (!GetSaveFileNameW(&ofn)) return {};
    return filename;
}

std::wstring SavePdfDialog() {
    wchar_t filename[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"PDF Document\0*.pdf\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"pdf";
    if (!GetSaveFileNameW(&ofn)) return {};
    return filename;
}

HWND GetHwnd(Window const& window) {
    HWND hwnd = nullptr;
    winrt::com_ptr<IWindowNative> native;
    winrt::check_hresult(window.as(IID_PPV_ARGS(native.put())));
    winrt::check_hresult(native->get_WindowHandle(&hwnd));
    return hwnd;
}

} // namespace

struct App::ui_state {
    Window window{ nullptr };
    Canvas canvas{ nullptr };
    Image image{ nullptr };
    TextBlock hint{ nullptr };
    StackPanel pagesPanel{ nullptr };
    Slider brightness{ nullptr };
    Slider contrast{ nullptr };
    Slider saturation{ nullptr };
    TextBlock brightness_label{ nullptr };
    TextBlock contrast_label{ nullptr };
    TextBlock saturation_label{ nullptr };
    DispatcherTimer adjustment_timer{ nullptr };

    // Handles for perspective crop
    std::array<Shapes::Ellipse, 4> handles{};
    Shapes::Polygon quad_polygon{ nullptr };
    Grid magnifier_grid{ nullptr };
    Image magnifier_image{ nullptr };
    Media::CompositeTransform magnifier_transform{ nullptr };
    int dragged_handle = -1;
    std::unique_ptr<engine> doc_engine;
    bool busy = false;

    static std::wstring format_percent(float v) {
        int iv = static_cast<int>(std::lroundf(v));
        return std::to_wstring(iv) + L"%";
    }

    Point MapImageToCanvas(double nx, double ny) {
        if (!image || !canvas) return {};
        double cw = canvas.ActualWidth();
        double ch = canvas.ActualHeight();
        double iw = image.Width();
        double ih = image.Height();
        if (iw <= 0 || ih <= 0 || cw <= 0 || ch <= 0) return {};
        
        double left = (cw - iw) / 2.0;
        double top = (ch - ih) / 2.0;
        return { static_cast<float>(left + nx * iw), static_cast<float>(top + ny * ih) };
    }

    Point MapCanvasToImage(Point const& pos) {
        if (!image || !canvas) return {};
        double cw = canvas.ActualWidth();
        double ch = canvas.ActualHeight();
        double iw = image.Width();
        double ih = image.Height();
        if (iw <= 0 || ih <= 0 || cw <= 0 || ch <= 0) return {};
        
        double left = (cw - iw) / 2.0;
        double top = (ch - ih) / 2.0;
        return { static_cast<float>(pos.X - left), static_cast<float>(pos.Y - top) };
    }

    void ShowHint() {
        if (image) image.Visibility(Visibility::Collapsed);
        if (hint) hint.Visibility(Visibility::Visible);
        for (auto& h : handles) if (h) h.Visibility(Visibility::Collapsed);
        if (quad_polygon) quad_polygon.Visibility(Visibility::Collapsed);
        if (magnifier_grid) magnifier_grid.Visibility(Visibility::Collapsed);
    }

    void ShowImage() {
        if (image) image.Visibility(Visibility::Visible);
        if (hint) hint.Visibility(Visibility::Collapsed);
        bool show_handles = doc_engine && doc_engine->page_count() > 0 && !doc_engine->is_warp_applied();
        for (auto& h : handles) if (h) {
            h.Visibility(show_handles ? Visibility::Visible : Visibility::Collapsed);
        }
        if (quad_polygon) {
            quad_polygon.Visibility(show_handles ? Visibility::Visible : Visibility::Collapsed);
        }
    }

    void PlaceHandles() {
        if (!image || !canvas || !doc_engine || doc_engine->page_count() == 0 || doc_engine->is_warp_applied()) return;
        canvas.UpdateLayout();
        double iw = image.ActualWidth();
        double ih = image.ActualHeight();
        if (iw <= 0 || ih <= 0) return;
        auto q = doc_engine->get_crop_quad().as_array();
        auto point_coll = Media::PointCollection();
        for (int i = 0; i < 4; ++i) {
            auto& h = handles[i];
            if (!h) continue;
            auto pt = MapImageToCanvas(q[i].x, q[i].y);
            point_coll.Append(pt);
            Canvas::SetLeft(h, pt.X - 7.0f);
            Canvas::SetTop(h, pt.Y - 7.0f);
        }
        if (quad_polygon) quad_polygon.Points(point_coll);
    }

    void FitImageToCanvas() {
        if (!image || !canvas) return;
        double cw = canvas.ActualWidth();
        double ch = canvas.ActualHeight();
        if (cw <= 0 || ch <= 0) return;

        // Get the source bitmap dimensions.
        auto src = image.Source();
        if (!src) return;
        auto bmp = src.try_as<BitmapSource>();
        if (!bmp) return;
        double srcW = bmp.PixelWidth();
        double srcH = bmp.PixelHeight();
        if (srcW <= 0 || srcH <= 0) return;

        // Compute uniform-scaled size that fits inside the canvas.
        double scale = std::min(cw / srcW, ch / srcH);
        double fitW = srcW * scale;
        double fitH = srcH * scale;

        // Set the Image element to this exact size — no internal letterboxing.
        image.Width(fitW);
        image.Height(fitH);

        // Center the Image on the canvas.
        Canvas::SetLeft(image, (cw - fitW) / 2.0);
        Canvas::SetTop(image, (ch - fitH) / 2.0);
        
        if (magnifier_image) {
            magnifier_image.Width(fitW);
            magnifier_image.Height(fitH);
        }
    }

    void BeginHandleDrag(int index) {
        if (!doc_engine || doc_engine->page_count() == 0) return;
        dragged_handle = index;
        if (magnifier_grid) {
            magnifier_grid.Visibility(Visibility::Visible);
            if (index == 0) { // tl -> br
                magnifier_grid.HorizontalAlignment(HorizontalAlignment::Right);
                magnifier_grid.VerticalAlignment(VerticalAlignment::Bottom);
            } else if (index == 1) { // tr -> bl
                magnifier_grid.HorizontalAlignment(HorizontalAlignment::Left);
                magnifier_grid.VerticalAlignment(VerticalAlignment::Bottom);
            } else if (index == 2) { // br -> tl
                magnifier_grid.HorizontalAlignment(HorizontalAlignment::Left);
                magnifier_grid.VerticalAlignment(VerticalAlignment::Top);
            } else if (index == 3) { // bl -> tr
                magnifier_grid.HorizontalAlignment(HorizontalAlignment::Right);
                magnifier_grid.VerticalAlignment(VerticalAlignment::Top);
            }
        }
    }

    void UpdateHandleDrag(Point const& pos) {
        if (dragged_handle < 0 || !image || !doc_engine) return;
        double iw = image.Width();
        double ih = image.Height();
        if (iw <= 0 || ih <= 0) return;
        auto imgPt = MapCanvasToImage(pos);
        float nx = static_cast<float>(std::clamp(imgPt.X / iw, 0.0, 1.0));
        float ny = static_cast<float>(std::clamp(imgPt.Y / ih, 0.0, 1.0));
        auto q = doc_engine->get_crop_quad();
        switch (dragged_handle) {
            case 0: q.tl = { nx, ny }; break;
            case 1: q.tr = { nx, ny }; break;
            case 2: q.br = { nx, ny }; break;
            case 3: q.bl = { nx, ny }; break;
        }
        doc_engine->set_crop_quad(q);
        
        if (magnifier_grid && magnifier_transform && magnifier_grid.Visibility() == Visibility::Visible) {
            double zoom = 2.0;
            double cx = nx * iw;
            double cy = ny * ih;
            // The mag_grid content area is 154x154 because of BorderThickness=3.
            // So the center of the content area is at 77.
            magnifier_transform.TranslateX(77.0 - cx * zoom);
            magnifier_transform.TranslateY(77.0 - cy * zoom);
        }

        PlaceHandles();
    }

    void EndHandleDrag() {
        dragged_handle = -1;
        if (magnifier_grid) magnifier_grid.Visibility(Visibility::Collapsed);
    }

    void RefreshPreview() {
        if (!image || !doc_engine || doc_engine->page_count() == 0) {
            ShowHint();
            return;
        }
        int w = 0, h = 0;
        auto rgba = doc_engine->render_current_page(w, h);
        if (rgba.empty() || w <= 0 || h <= 0) {
            ShowHint();
            return;
        }
        rgba_to_bgra(rgba);
        auto bitmap = make_writeable_bitmap(w, h, rgba.data());
        image.Source(bitmap);
        if (magnifier_image) magnifier_image.Source(bitmap);
        ShowImage();
        FitImageToCanvas();
        PlaceHandles();
    }

    void EnsureEngine() {
        if (!doc_engine) doc_engine = std::make_unique<engine>();
    }

    bool AddPage(const std::wstring& path) {
        try {
            int w = 0, h = 0;
            auto bgra = wic_decode_file_to_bgra(path, w, h);
            if (bgra.empty()) return false;
            auto rgba = bgra;
            bgra_to_rgba(rgba);
            EnsureEngine();
            doc_engine->load_image(w, h, rgba.data());
            auto idx = doc_engine->page_count() - 1;
            doc_engine->set_current_page(idx);
            // Start in original-perspective mode; user triggers corner detection.
            doc_engine->set_warp_applied(false);
            return true;
        } catch (...) {
            return false;
        }
    }

    void MagicPerspective() {
        if (!doc_engine || doc_engine->page_count() == 0) return;
        auto q = doc_engine->detect_document_corners();
        doc_engine->set_crop_quad(q);
        doc_engine->set_warp_applied(false);
        RefreshPreview();
        PlaceHandles();
    }

    void ApplyPerspective() {
        if (!doc_engine || doc_engine->page_count() == 0) return;
        doc_engine->set_warp_applied(true);
        RefreshPreview();
    }

    void RotateCurrentPage(int steps) {
        if (!doc_engine || doc_engine->page_count() == 0) return;
        doc_engine->rotate_current_page(steps);
        RefreshPreview();
        PlaceHandles();
    }

    void FlipCurrentPage(bool horizontal) {
        if (!doc_engine || doc_engine->page_count() == 0) return;
        doc_engine->flip_current_page(horizontal);
        RefreshPreview();
        PlaceHandles();
    }

    void SelectPage(std::size_t index) {
        if (!doc_engine || index >= doc_engine->page_count()) return;
        doc_engine->set_current_page(index);
        if (brightness) brightness.Value(doc_engine->get_brightness() * 100.0);
        if (contrast) contrast.Value(doc_engine->get_contrast() * 100.0);
        if (saturation) saturation.Value(doc_engine->get_saturation() * 100.0);
        if (brightness_label) brightness_label.Text(format_percent(doc_engine->get_brightness() * 100.0f).c_str());
        if (contrast_label) contrast_label.Text(format_percent(doc_engine->get_contrast() * 100.0f).c_str());
        if (saturation_label) saturation_label.Text(format_percent(doc_engine->get_saturation() * 100.0f).c_str());
        RefreshPreview();
        RefreshPagesList();
    }

    void RefreshPagesList() {
        if (!pagesPanel) return;
        pagesPanel.Children().Clear();
        if (!doc_engine) return;
        auto count = doc_engine->page_count();
        auto current = doc_engine->current_page_index();
        for (std::size_t i = 0; i < count; ++i) {
            ToggleButton b;
            b.Content(box_value((std::to_wstring(i + 1)).c_str()));
            b.Width(64);
            b.Height(64);
            b.CornerRadius(CornerRadius{8, 8, 8, 8});
            b.Margin(ThicknessHelper::FromUniformLength(4));
            b.IsChecked(i == current);
            b.Click([this, i](auto&&, auto&&) { SelectPage(i); });
            pagesPanel.Children().Append(b);
        }
    }

    void RemoveCurrentPage() {
        if (!doc_engine) return;
        auto idx = doc_engine->current_page_index();
        doc_engine->remove_page(idx);
        if (doc_engine->page_count() == 0) {
            doc_engine.reset();
            ShowHint();
        } else {
            RefreshPreview();
        }
        RefreshPagesList();
    }

    void MoveCurrentPage(int delta) {
        if (!doc_engine) return;
        auto from = doc_engine->current_page_index();
        auto to = static_cast<std::size_t>(static_cast<int>(from) + delta);
        if (to >= doc_engine->page_count()) return;
        doc_engine->move_page(from, to);
        doc_engine->set_current_page(to);
        RefreshPreview();
        RefreshPagesList();
    }

    void LoadDroppedImages(const std::vector<std::wstring>& paths) {
        if (paths.empty()) return;
        bool first = true;
        for (const auto& p : paths) {
            if (!IsImageFile(p)) continue;
            if (AddPage(p)) {
                if (first) {
                    first = false;
                    if (brightness) brightness.Value(0.0);
                    if (contrast) contrast.Value(0.0);
                    if (saturation) saturation.Value(0.0);
                }
            }
        }
        RefreshPreview();
        RefreshPagesList();
    }

    void SetFilter(filter_preset f) {
        if (!doc_engine || doc_engine->page_count() == 0) return;
        doc_engine->set_filter(f);
        RefreshPreview();
    }

    void UpdateAdjustmentLabel() {
        if (brightness_label) brightness_label.Text(format_percent(static_cast<float>(brightness.Value())).c_str());
        if (contrast_label) contrast_label.Text(format_percent(static_cast<float>(contrast.Value())).c_str());
        if (saturation_label) saturation_label.Text(format_percent(static_cast<float>(saturation.Value())).c_str());
    }

    void ApplyAdjustment() {
        if (!doc_engine || doc_engine->page_count() == 0) return;
        if (brightness) doc_engine->set_brightness(static_cast<float>(brightness.Value()) / 100.0f);
        if (contrast) doc_engine->set_contrast(static_cast<float>(contrast.Value()) / 100.0f);
        if (saturation) doc_engine->set_saturation(static_cast<float>(saturation.Value()) / 100.0f);
        UpdateAdjustmentLabel();
        RefreshPreview();
    }

    void ExportImage(bool jpeg) {
        if (!doc_engine || doc_engine->page_count() == 0) return;
        auto path = SaveImageDialog(jpeg);
        if (path.empty()) return;
        int w = 0, h = 0;
        auto rgba = doc_engine->render_current_page(w, h);
        if (rgba.empty()) return;
        rgba_to_bgra(rgba);
        wic_encode_file(path, rgba.data(), w, h, jpeg, jpeg ? 90 : 0);
    }

    void ExportPdf() {
        if (!doc_engine || doc_engine->page_count() == 0) return;
        auto path = SavePdfDialog();
        if (path.empty()) return;
        auto pdf = doc_engine->export_pdf(300, 90);
        if (pdf.empty()) return;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(h, pdf.data(), static_cast<DWORD>(pdf.size()), &written, nullptr);
        CloseHandle(h);
    }
};

void App::OnLaunched(LaunchActivatedEventArgs const&) {
    auto state = std::make_shared<ui_state>();

    // Parse command line.
    std::wstring auto_load_path;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg(argv[i]);
            if (auto_load_path.empty() && IsImageFile(arg)) {
                auto_load_path = arg;
            }
        }
        LocalFree(argv);
    }

    state->adjustment_timer = DispatcherTimer();
    state->adjustment_timer.Interval(TimeSpan{ 2000000 }); // 200 ms
    state->adjustment_timer.Tick([state](auto&&, auto&&) {
        state->adjustment_timer.Stop();
        state->ApplyAdjustment();
    });

    // ---- Sidebar ----
    auto sidebar = StackPanel();
    sidebar.Padding(ThicknessHelper::FromUniformLength(16));
    sidebar.Spacing(12.0);

    auto title = TextBlock();
    title.Text(SCANWISE_TITLE);
    title.FontSize(24);
    title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    title.Margin(ThicknessHelper::FromLengths(0, 0, 0, 12));
    sidebar.Children().Append(title);

    auto make_header = [](const wchar_t* text) {
        TextBlock tb;
        tb.Text(text);
        tb.FontSize(14);
        tb.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        tb.Margin(ThicknessHelper::FromLengths(0, 8, 0, 4));
        return tb;
    };

    auto make_icon_btn = [](const wchar_t* glyph, const wchar_t* tooltip) {
        Button b;
        FontIcon icon;
        icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
        icon.Glyph(glyph);
        b.Content(icon);
        b.Width(36);
        b.Height(36);
        b.Padding(ThicknessHelper::FromUniformLength(0));
        ToolTipService::SetToolTip(b, box_value(tooltip));
        return b;
    };

    auto make_text_icon_btn = [](const wchar_t* glyph, const wchar_t* text, const wchar_t* tooltip = nullptr) {
        Button b;
        StackPanel sp;
        sp.Orientation(Orientation::Horizontal);
        sp.Spacing(8);
        FontIcon icon;
        icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
        icon.Glyph(glyph);
        sp.Children().Append(icon);
        TextBlock tb;
        tb.Text(text);
        tb.VerticalAlignment(VerticalAlignment::Center);
        sp.Children().Append(tb);
        b.Content(sp);
        b.HorizontalAlignment(HorizontalAlignment::Stretch);
        if (tooltip) ToolTipService::SetToolTip(b, box_value(tooltip));
        return b;
    };

    // Pages section
    sidebar.Children().Append(make_header(L"Pages"));

    auto pagesPanel = StackPanel();
    pagesPanel.Orientation(Orientation::Horizontal);
    pagesPanel.HorizontalAlignment(HorizontalAlignment::Center);
    state->pagesPanel = pagesPanel;

    auto pagesScroll = ScrollViewer();
    pagesScroll.HorizontalScrollMode(ScrollMode::Enabled);
    pagesScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
    pagesScroll.VerticalScrollMode(ScrollMode::Disabled);
    pagesScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Disabled);
    pagesScroll.Content(pagesPanel);
    pagesScroll.VerticalAlignment(VerticalAlignment::Bottom);
    pagesScroll.HorizontalAlignment(HorizontalAlignment::Center);
    pagesScroll.Margin(ThicknessHelper::FromLengths(0, 0, 0, 16));
    // pagesScroll will be added to the canvas pane later

    auto pageButtons = StackPanel();
    pageButtons.Orientation(Orientation::Horizontal);
    pageButtons.Spacing(4.0);

    auto btnAdd = make_icon_btn(L"\xE710", L"Add Page");
    btnAdd.Click([state](auto&&, auto&&) {
        auto path = OpenImageDialog();
        if (!path.empty()) state->LoadDroppedImages({ path });
    });
    pageButtons.Children().Append(btnAdd);

    auto btnRemove = make_icon_btn(L"\xE74D", L"Remove Page");
    btnRemove.Click([state](auto&&, auto&&) { state->RemoveCurrentPage(); });
    pageButtons.Children().Append(btnRemove);

    auto btnUp = make_icon_btn(L"\xE74A", L"Move Up");
    btnUp.Click([state](auto&&, auto&&) { state->MoveCurrentPage(-1); });
    pageButtons.Children().Append(btnUp);

    auto btnDown = make_icon_btn(L"\xE74B", L"Move Down");
    btnDown.Click([state](auto&&, auto&&) { state->MoveCurrentPage(1); });
    pageButtons.Children().Append(btnDown);

    sidebar.Children().Append(pageButtons);

    // Filters (Presets)
    sidebar.Children().Append(make_header(L"Presets"));

    Grid presetGrid;
    presetGrid.ColumnDefinitions().Append(ColumnDefinition());
    presetGrid.ColumnDefinitions().Append(ColumnDefinition());
    presetGrid.RowDefinitions().Append(RowDefinition());
    presetGrid.RowDefinitions().Append(RowDefinition());
    presetGrid.RowDefinitions().Append(RowDefinition()); // 3rd row for Magic Color
    presetGrid.ColumnSpacing(4.0);
    presetGrid.RowSpacing(4.0);

    auto make_preset = [&](const wchar_t* label, filter_preset f, int row, int col, int colSpan = 1) {
        Button b;
        b.Content(box_value(label));
        b.HorizontalAlignment(HorizontalAlignment::Stretch);
        b.Click([state, f](auto&&, auto&&) { state->SetFilter(f); });
        Grid::SetRow(b, row);
        Grid::SetColumn(b, col);
        if (colSpan > 1) Grid::SetColumnSpan(b, colSpan);
        presetGrid.Children().Append(b);
    };
    make_preset(L"Original", filter_preset::original, 0, 0);
    make_preset(L"B & W", filter_preset::black_white, 0, 1);
    make_preset(L"Document", filter_preset::document, 1, 0);
    make_preset(L"Photo", filter_preset::photo, 1, 1);
    make_preset(L"Magic Color \u2728", filter_preset::magic_color, 2, 0, 2); // Sparkle emoji
    sidebar.Children().Append(presetGrid);

    // Perspective
    sidebar.Children().Append(make_header(L"Perspective"));

    auto btnMagic = make_text_icon_btn(L"\xE7A8", L"Magic Perspective");
    winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(btnMagic, L"Magic Perspective"); // CRITICAL for tests
    btnMagic.Click([state](auto&&, auto&&) { state->MagicPerspective(); });
    sidebar.Children().Append(btnMagic);

    auto btnApply = make_text_icon_btn(L"\xE73E", L"Apply Perspective");
    winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(btnApply, L"Apply Perspective"); // CRITICAL for tests
    btnApply.Click([state](auto&&, auto&&) { state->ApplyPerspective(); });
    sidebar.Children().Append(btnApply);

    // Adjustments
    sidebar.Children().Append(make_header(L"Adjustments"));

    auto make_slider = [&](const wchar_t* label, double min, double max, double step) {
        auto sp = StackPanel();
        auto header = Grid();
        auto cd1 = ColumnDefinition();
        auto cd2 = ColumnDefinition();
        cd2.Width(GridLengthHelper::Auto());
        header.ColumnDefinitions().Append(cd1);
        header.ColumnDefinitions().Append(cd2);
        
        auto tb = TextBlock();
        tb.Text(label);
        tb.FontSize(12);
        tb.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(tb, 0);
        header.Children().Append(tb);
        
        auto val = TextBlock();
        val.Text(L"0%");
        val.FontSize(12);
        val.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(val, 1);
        header.Children().Append(val);
        
        sp.Children().Append(header);
        
        Slider s;
        s.Minimum(min);
        s.Maximum(max);
        s.Value(0.0);
        s.StepFrequency(step);
        s.SmallChange(step);
        s.LargeChange(step * 10.0);
        s.HorizontalAlignment(HorizontalAlignment::Stretch);
        s.Margin(ThicknessHelper::FromLengths(0, -4, 0, 0));
        s.ValueChanged([state](auto&&, auto&&) {
            state->UpdateAdjustmentLabel();
            if (state->adjustment_timer) {
                state->adjustment_timer.Stop();
                state->adjustment_timer.Start();
            }
        });
        sp.Children().Append(s);
        return std::tuple{sp, s, val};
    };

    auto [brightPanel, brightSlider, brightLabel] = make_slider(L"Brightness", -100.0, 100.0, 1.0);
    state->brightness = brightSlider;
    state->brightness_label = brightLabel;
    sidebar.Children().Append(brightPanel);

    auto [contrastPanel, contrastSlider, contrastLabel] = make_slider(L"Contrast", -100.0, 100.0, 1.0);
    state->contrast = contrastSlider;
    state->contrast_label = contrastLabel;
    sidebar.Children().Append(contrastPanel);

    auto [satPanel, satSlider, satLabel] = make_slider(L"Saturation", -100.0, 100.0, 1.0);
    state->saturation = satSlider;
    state->saturation_label = satLabel;
    sidebar.Children().Append(satPanel);

    // Transform
    sidebar.Children().Append(make_header(L"Transform"));

    auto transformPanel = StackPanel();
    transformPanel.Orientation(Orientation::Horizontal);
    transformPanel.Spacing(4.0);

    auto make_small_btn = [](const wchar_t* text, auto handler) {
        Button b;
        b.Content(box_value(text));
        b.Click(handler);
        return b;
    };

    auto make_flip_btn = [](bool horizontal, const wchar_t* tooltip) {
        Button b;
        TextBlock tb;
        tb.Text(horizontal ? L"\x25C0 \x25B6" : L"\x25B2\n\x25BC"); // ◀ ▶ or ▲ ▼
        tb.FontSize(10);
        tb.TextAlignment(TextAlignment::Center);
        tb.VerticalAlignment(VerticalAlignment::Center);
        tb.HorizontalAlignment(HorizontalAlignment::Center);
        b.Content(tb);
        b.Width(36);
        b.Height(36);
        b.Padding(ThicknessHelper::FromUniformLength(0));
        ToolTipService::SetToolTip(b, box_value(tooltip));
        return b;
    };

    auto btnRotate = make_icon_btn(L"\xE7AD", L"Rotate 90 CW");
    btnRotate.Click([state](auto&&, auto&&) { state->RotateCurrentPage(1); });
    transformPanel.Children().Append(btnRotate);

    auto btnFlipH = make_flip_btn(true, L"Flip Horizontal");
    btnFlipH.Click([state](auto&&, auto&&) { state->FlipCurrentPage(true); });
    transformPanel.Children().Append(btnFlipH);

    auto btnFlipV = make_flip_btn(false, L"Flip Vertical");
    btnFlipV.Click([state](auto&&, auto&&) { state->FlipCurrentPage(false); });
    transformPanel.Children().Append(btnFlipV);

    sidebar.Children().Append(transformPanel);

    // Export
    sidebar.Children().Append(make_header(L"Export"));

    auto exportPanel = StackPanel();
    exportPanel.Orientation(Orientation::Horizontal);
    exportPanel.Spacing(4.0);
    
    exportPanel.Children().Append(make_small_btn(L"PNG", [state](auto&&, auto&&) { state->ExportImage(false); }));
    exportPanel.Children().Append(make_small_btn(L"JPEG", [state](auto&&, auto&&) { state->ExportImage(true); }));
    exportPanel.Children().Append(make_small_btn(L"PDF", [state](auto&&, auto&&) { state->ExportPdf(); }));

    sidebar.Children().Append(exportPanel);



    // ---- Main canvas ----
    auto rightPane = Grid();

    auto canvas = Canvas();
    state->canvas = canvas;
    rightPane.Children().Append(canvas);

    auto hint = TextBlock();
    hint.Text(L"Drop images here or paste from clipboard (Ctrl+V)");
    hint.HorizontalAlignment(HorizontalAlignment::Center);
    hint.VerticalAlignment(VerticalAlignment::Center);
    hint.Foreground(LightTextBrush());
    hint.FontSize(18);
    state->hint = hint;
    rightPane.Children().Append(hint);

    auto image = Image();
    image.Stretch(Stretch::Uniform);
    image.Visibility(Visibility::Collapsed);
    state->image = image;
    canvas.Children().Append(image);

    auto quad_polygon = Shapes::Polygon();
    quad_polygon.Stroke(SolidBrush(0, 120, 215));
    quad_polygon.StrokeThickness(2.0);
    quad_polygon.Visibility(Visibility::Collapsed);
    state->quad_polygon = quad_polygon;
    canvas.Children().Append(quad_polygon);

    auto mag_grid = Grid();
    mag_grid.Width(160);
    mag_grid.Height(160);
    mag_grid.CornerRadius(CornerRadius{80, 80, 80, 80});
    mag_grid.BorderBrush(SolidBrush(255, 255, 255));
    mag_grid.BorderThickness(Thickness{3, 3, 3, 3});
    mag_grid.Background(SolidBrush(30, 30, 30));
    mag_grid.Visibility(Visibility::Collapsed);
    mag_grid.Margin(Thickness{20, 20, 20, 20});
    
    auto mag_image = Image();
    mag_image.HorizontalAlignment(HorizontalAlignment::Left);
    mag_image.VerticalAlignment(VerticalAlignment::Top);
    mag_image.RenderTransformOrigin(Windows::Foundation::Point{0.0f, 0.0f});
    mag_image.Stretch(Stretch::Fill);
    
    auto mag_transform = Media::CompositeTransform();
    mag_transform.CenterX(0.0);
    mag_transform.CenterY(0.0);
    mag_transform.ScaleX(2.0);
    mag_transform.ScaleY(2.0);
    mag_image.RenderTransform(mag_transform);
    
    auto inner_canvas = Canvas();
    inner_canvas.Children().Append(mag_image);
    mag_grid.Children().Append(inner_canvas);

    auto ch_h = Shapes::Line();
    ch_h.X1(67); ch_h.Y1(77); ch_h.X2(87); ch_h.Y2(77);
    ch_h.Stroke(SolidBrush(0, 255, 0)); ch_h.StrokeThickness(1.5);
    mag_grid.Children().Append(ch_h);
    
    auto ch_v = Shapes::Line();
    ch_v.X1(77); ch_v.Y1(67); ch_v.X2(77); ch_v.Y2(87);
    ch_v.Stroke(SolidBrush(0, 255, 0)); ch_v.StrokeThickness(1.5);
    mag_grid.Children().Append(ch_v);

    state->magnifier_grid = mag_grid;
    state->magnifier_image = mag_image;
    state->magnifier_transform = mag_transform;
    rightPane.Children().Append(mag_grid);

    // Corner handles for manual crop correction.
    for (int i = 0; i < 4; ++i) {
        Shapes::Ellipse e;
        e.Width(14);
        e.Height(14);
        e.Fill(SolidBrush(0, 120, 215));
        e.Stroke(SolidBrush(255, 255, 255));
        e.StrokeThickness(2.0);
        e.Visibility(Visibility::Collapsed);
        e.PointerPressed([state, i](IInspectable const&, PointerRoutedEventArgs const& e) {
            state->BeginHandleDrag(i);
            state->canvas.CapturePointer(e.Pointer());
            e.Handled(true);
        });
        state->handles[i] = e;
        canvas.Children().Append(e);
    }
    
    // Add the pages carousel to the canvas pane
    rightPane.Children().Append(pagesScroll);

    // ---- Layout ----
    auto root = Grid();
    root.RowDefinitions().Append(RowDefinition()); // Title bar row
    root.RowDefinitions().Append(RowDefinition()); // Main content row
    root.RowDefinitions().GetAt(0).Height(GridLengthHelper::Auto());
    root.RowDefinitions().GetAt(1).Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));

    auto appTitleBar = Grid();
    appTitleBar.Height(32);
    auto titleText = TextBlock();
    titleText.Text(SCANWISE_TITLE);
    titleText.VerticalAlignment(VerticalAlignment::Center);
    titleText.Margin(ThicknessHelper::FromLengths(16, 0, 0, 0));
    titleText.FontSize(12);
    appTitleBar.Children().Append(titleText);
    Grid::SetRow(appTitleBar, 0);
    root.Children().Append(appTitleBar);

    auto mainContent = Grid();
    auto leftCol = ColumnDefinition();
    leftCol.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    auto rightCol = ColumnDefinition();
    rightCol.Width(GridLengthHelper::FromPixels(280));
    mainContent.ColumnDefinitions().Append(leftCol);
    mainContent.ColumnDefinitions().Append(rightCol);

    auto sidebarScroll = ScrollViewer();
    sidebarScroll.Content(sidebar);
    sidebarScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    sidebarScroll.HorizontalScrollMode(ScrollMode::Disabled);
    
    // Canvas on the left (col 0), Sidebar on the right (col 1)
    Grid::SetColumn(rightPane, 0);
    Grid::SetColumn(sidebarScroll, 1);
    
    mainContent.Children().Append(rightPane);
    mainContent.Children().Append(sidebarScroll);
    Grid::SetRow(mainContent, 1);
    root.Children().Append(mainContent);

    // ---- Drag & drop ----
    canvas.AllowDrop(true);
    canvas.DragOver([](IInspectable const&, DragEventArgs const& e) {
        e.AcceptedOperation(DataPackageOperation::Copy);
        e.Handled(true);
    });
    canvas.Drop([this, state](IInspectable const&, DragEventArgs const& e) -> fire_and_forget {
        auto lifetime = get_strong();
        e.Handled(true);
        auto view = e.DataView();
        if (!view.Contains(StandardDataFormats::StorageItems())) co_return;
        auto items = co_await view.GetStorageItemsAsync();
        std::vector<std::wstring> paths;
        for (auto item : items) {
            auto p = std::wstring(item.Path());
            if (IsImageFile(p)) paths.push_back(p);
        }
        if (!paths.empty()) state->LoadDroppedImages(paths);
    });

    canvas.PointerMoved([state](IInspectable const&, PointerRoutedEventArgs const& e) {
        if (state->dragged_handle < 0) return;
        auto point = e.GetCurrentPoint(state->canvas);
        state->UpdateHandleDrag(point.Position());
        e.Handled(true);
    });
    canvas.PointerReleased([state](IInspectable const&, PointerRoutedEventArgs const& e) {
        if (state->dragged_handle < 0) return;
        state->canvas.ReleasePointerCapture(e.Pointer());
        state->EndHandleDrag();
        e.Handled(true);
    });
    canvas.SizeChanged([state](IInspectable const&, SizeChangedEventArgs const&) {
        state->FitImageToCanvas();
        state->PlaceHandles();
    });

    // ---- Keyboard paste ----
    root.KeyDown([this, state](IInspectable const&, KeyRoutedEventArgs const& e) -> fire_and_forget {
        auto lifetime = get_strong();
        if (e.Key() != VirtualKey::V) co_return;
        auto mods = winrt::Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(VirtualKey::Control);
        if ((static_cast<uint32_t>(mods) & static_cast<uint32_t>(VirtualKeyModifiers::Control)) == 0) co_return;
        e.Handled(true);

        auto view = Clipboard::GetContent();
        if (!view.Contains(StandardDataFormats::Bitmap())) co_return;
        auto ref = co_await view.GetBitmapAsync();
        auto stream = co_await ref.OpenReadAsync();
        winrt::com_ptr<IStream> comStream;
        winrt::check_hresult(CreateStreamOverRandomAccessStream(
            winrt::get_unknown(stream), IID_PPV_ARGS(comStream.put())));
        int w = 0, h = 0;
        auto bgra = wic_decode_stream_to_bgra(comStream.get(), w, h);
        if (bgra.empty()) co_return;
        auto rgba = bgra;
        bgra_to_rgba(rgba);
        state->EnsureEngine();
        state->doc_engine->load_image(w, h, rgba.data());
        auto idx = state->doc_engine->page_count() - 1;
        state->doc_engine->set_current_page(idx);
        auto q = state->doc_engine->detect_document_corners();
        state->doc_engine->set_crop_quad(q);
        state->RefreshPreview();
        state->RefreshPagesList();
    });

    Window window;
    window.Title(SCANWISE_TITLE);
    window.ExtendsContentIntoTitleBar(true);
    window.SetTitleBar(appTitleBar);
    window.SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop());
    window.Content(root);
    window.Activate();
    // Give the window a comfortable initial size.
    SetWindowPos(GetHwnd(window), nullptr, 120, 40, 1320, 920,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    state->window = window;

    // Auto-load an image passed on the command line (useful for testing).
    if (!auto_load_path.empty()) {
        state->AddPage(auto_load_path);
        state->MagicPerspective();
    }
}

} // namespace scanwise
