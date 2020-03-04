// SPDX-License-Identifier: GPL-3.0-only

#include <QCloseEvent>
#include <QMessageBox>
#include <QMenuBar>
#include <QScrollArea>
#include <QLabel>
#include <QApplication>
#include <QScreen>
#include <QVBoxLayout>
#include <QSpacerItem>
#include <filesystem>
#include <invader/file/file.hpp>
#include <invader/tag/parser/parser.hpp>
#include "tag_tree_window.hpp"
#include "tag_editor_widget.hpp"
#include "tag_editor_edit_widget_view.hpp"

namespace Invader::EditQt {
    TagEditorWindow::TagEditorWindow(QWidget *parent, TagTreeWindow *parent_window, const File::TagFile &tag_file) : QMainWindow(parent), parent_window(parent_window), file(tag_file) {
        this->make_dirty(false);

        auto open_file = File::open_file(tag_file.full_path.string().c_str());
        if(!open_file.has_value()) {
            char formatted_error[1024];
            std::snprintf(formatted_error, sizeof(formatted_error), "Failed to open %s. Make sure it exists and you have permission to open it.", tag_file.full_path.string().c_str());
            QMessageBox(QMessageBox::Icon::Critical, "Error", formatted_error, QMessageBox::Ok, this).exec();
            this->close();
            return;
        }
        try {
            this->parser_data = Parser::ParserStruct::parse_hek_tag_file(open_file->data(), open_file->size(), false).release();
        }
        catch(std::exception &e) {
            char formatted_error[1024];
            std::snprintf(formatted_error, sizeof(formatted_error), "Failed to open %s due to an exception error:\n\n%s", tag_file.full_path.string().c_str(), e.what());
            QMessageBox(QMessageBox::Icon::Critical, "Error", formatted_error, QMessageBox::Ok, this).exec();
            this->close();
            return;
        }

        // Make and set our menu bar
        QMenuBar *bar = new QMenuBar(this);
        this->setMenuBar(bar);

        // File menu
        auto *file_menu = bar->addMenu("File");

        auto *save = file_menu->addAction("Save");
        save->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
        save->setShortcut(QKeySequence::Save);
        connect(save, &QAction::triggered, this, &TagEditorWindow::perform_save);

        auto *save_as = file_menu->addAction("Save as...");
        save_as->setIcon(QIcon::fromTheme(QStringLiteral("document-save-as")));
        save_as->setShortcut(QKeySequence::SaveAs);
        connect(save_as, &QAction::triggered, this, &TagEditorWindow::perform_save_as);

        file_menu->addSeparator();

        auto *close = file_menu->addAction("Close");
        close->setShortcut(QKeySequence::Close);
        close->setIcon(QIcon::fromTheme(QStringLiteral("document-close")));
        connect(close, &QAction::triggered, this, &TagEditorWindow::close);

        // Set up the scroll area and widgets
        auto *scroll_view = new QScrollArea;
        scroll_view->setWidgetResizable(true);
        auto values = std::vector<Parser::ParserStructValue>(this->parser_data->get_values());
        this->setCentralWidget(scroll_view);
        auto *view = new TagEditorEditWidgetView(nullptr, values, this, true);
        scroll_view->setWidget(view);

        // Lock the scroll view and window to a set width
        int max_width = scroll_view->widget()->width() + qApp->style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 50;

        // Center this
        this->setGeometry(
            QStyle::alignedRect(
                Qt::LeftToRight,
                Qt::AlignCenter,
                QSize(max_width, 600),
                QGuiApplication::primaryScreen()->geometry()
            )
        );

        // We did it!
        this->successfully_opened = true;
    }

    void TagEditorWindow::closeEvent(QCloseEvent *event) {
        bool accept;
        if(dirty) {
            char message_entire_text[512];
            std::snprintf(message_entire_text, sizeof(message_entire_text), "This file \"%s\" has been modified.\nDo you want to save your changes?", this->file.full_path.string().c_str());
            QMessageBox are_you_sure(QMessageBox::Icon::Question, "Unsaved changes", message_entire_text, QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
            switch(are_you_sure.exec()) {
                case QMessageBox::Accepted:
                    accept = this->perform_save();
                    break;
                case QMessageBox::Cancel:
                    accept = false;
                    break;
                case QMessageBox::Discard:
                    accept = true;
                    break;
                default:
                    std::terminate();
            }
        }
        else {
            accept = true;
        }

        event->setAccepted(accept);
    }

    bool TagEditorWindow::perform_save() {
        // Save; benchmark
        auto start = std::chrono::steady_clock::now();
        auto tag_data = parser_data->generate_hek_tag_data(this->file.tag_class_int);
        auto result = Invader::File::save_file(this->file.full_path.string().c_str(), tag_data);
        if(!result) {
            std::fprintf(stderr, "perform_save() failed\n");
        }
        else {
            this->make_dirty(false);
            auto end = std::chrono::steady_clock::now();
            std::printf("Saved %s in %zu ms\n", this->get_file().full_path.string().c_str(), std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
        }
        return result;
    }

    bool TagEditorWindow::perform_save_as() {
        std::fprintf(stderr, "TODO: perform_save_as()\n");
        return false;
    }

    bool TagEditorWindow::perform_refactor() {
        std::fprintf(stderr, "TODO: perform_refactor()\n");
        return false;
    }

    void TagEditorWindow::make_dirty(bool dirty) {
        this->dirty = dirty;
        char title_bar[512];
        const char *asterisk = dirty ? " *" : "";
        std::snprintf(title_bar, sizeof(title_bar), "%s%s", this->file.tag_path.c_str(), asterisk);
        this->setWindowTitle(title_bar);
    }

    const File::TagFile &TagEditorWindow::get_file() const noexcept {
        return this->file;
    }

    TagEditorWindow::~TagEditorWindow() {
        delete this->parser_data;
    }
}
