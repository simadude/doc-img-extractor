#include <iostream>
#include <string>
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Progress.H>
#include <cstdlib>
#include <vector>

bool support_DJVU = true;
bool support_PDF = true;
bool support_DOC = true;
bool support_EPUB = true;


Fl_Double_Window* wstart = nullptr;
Fl_Text_Display* l = nullptr;
Fl_Button* bc = nullptr;
Fl_Button* ba = nullptr;

Fl_Double_Window* wmain = nullptr;
Fl_Double_Window* wfile_browser = nullptr;
Fl_Button* b_input_files = nullptr;
Fl_Box* b_input_files_count = nullptr;
Fl_Button* b_output_dir = nullptr;
Fl_Box* b_output_dir_label = nullptr;
Fl_Progress* progress_bar = nullptr; // New: Progress bar
Fl_Button* startb = nullptr; // New: Start button

std::string input_files_count_str;
std::vector<std::string> input_files_vec; 
std::string output_dir_str;

int call(std::string c) {
    std::string buffer;
    buffer.append(c);
    buffer.append(" > /dev/null 2>&1");
    return system(buffer.c_str());
};

int check_dependencies() {
    int dep_count = 0;

    // Check: ddjvu --help (expected 256)
    {
        int res = call("ddjvu --help");
        if (res == 32512) {
            l->insert("Command not found: ddjvu. Make sure to install djvulibre or djvulibre-bin package\n");
            support_DJVU = false;
        }
        else if (res == 256) dep_count++;
        else {
            std::string msg = std::string("Unexpected exitcode (") + std::to_string(res) + "), expected 256 from ddjvu --help\n";
            l->insert(msg.c_str());
            support_DJVU = false;
        }
    }

    // Check: djvused --help (expected 2560)
    {
        int res = call("djvused --help");
        if (res == 32512) {
            l->insert("Command not found: djvused. Make sure to install djvulibre or djvulibre-bin package\n");
            support_DJVU = false;
        }
        else if (res == 2560) dep_count++;
        else {
            std::string msg = std::string("Unexpected exitcode (") + std::to_string(res) + "), expected 2560 from djvused --help\n";
            l->insert(msg.c_str());
            support_DJVU = false;
        }
    }

    // Check: soffice --version (expected 0)
    {
        int res = call("soffice --version");
        if (res == 32512) {
            l->insert("Command not found: soffice. Make sure to install libreoffice package\n");
            support_DOC = false;
            support_EPUB = false;
        }
        else if (res == 0) dep_count++;
        else {
            std::string msg = std::string("Unexpected exitcode (") + std::to_string(res) + "), expected 0 from soffice --version\n";
            l->insert(msg.c_str());
            support_DOC = false;
            support_EPUB = false;
        }
    }

    // Check: pdfimages -v (expected 0)
    {
        int res = call("pdfimages -v");
        if (res == 32512) {
            l->insert("Command not found: pdfimages. Make sure to install poppler-utils\n");
            support_PDF = false;
        }
        else if (res == 0) dep_count++;
        else {
            std::string msg = std::string("Unexpected exitcode (") + std::to_string(res) + "), expected 0 from pdfimages -v\n";
            l->insert(msg.c_str());
            support_PDF = false;
        }
    }

    // Check: unzip (expected 0 or non-zero for usage)
    {
        int res = call("unzip");
        if (res == 32512) {
            l->insert("Command not found: unzip. Make sure to install unzip package\n");
            support_DOC = false;
            support_EPUB = false;
        } else {
            dep_count++;
        }
    }

    return dep_count;
}

void check_deps(void* user_data) {
    const int total_deps = 5;

    l->insert("Checking dependencies...\n");
    int dep_count = check_dependencies();
    {
        std::string msg = std::string("Dependencies: ") + std::to_string(dep_count) + "/" + std::to_string(total_deps) + "\n";
        l->insert(msg.c_str());
    }
    
    if (dep_count == 0) {
        l->insert("No dependencies found. Please install dependencies and restart in order to continue.");
    } else if (dep_count < total_deps) {
        l->insert("Some dependencies were not found. Would you like to still continue?");
        bc->show();
    } else {
        l->insert("All dependencies found!");
        wstart->hide();
        wmain->show();
        // bc->show();
    }
}

static void quit_cb(Fl_Widget* o) {
    exit(0);
}

static void bc_cb(Fl_Widget* o) {
    wstart->hide();
    wmain->show();
}

static void input_docs_cb(Fl_Widget* o) {
    Fl_Native_File_Chooser infc;
    infc.title("Choose documents to extract images from");
    infc.type(Fl_Native_File_Chooser::BROWSE_MULTI_FILE);
    infc.directory(".");
    if (infc.show() == 0) { // 0 means file chosen (not error or cancel)
        input_files_count_str = std::to_string(infc.count()) + std::string(" file(s)");
        b_input_files_count->label(input_files_count_str.c_str());
        b_input_files_count->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        input_files_vec.clear();
        for (int i = 0; i < infc.count(); i++) {
            input_files_vec.push_back(std::string(infc.filename(i)));
        }
    }
}

static void output_dir_cb(Fl_Widget* o) {
    Fl_Native_File_Chooser onfc;
    onfc.title("Choose directory to save images to");
    onfc.type(Fl_Native_File_Chooser::BROWSE_DIRECTORY);
    onfc.directory(".");
    if (onfc.show() == 0) { // 0 means file chosen (not error or cancel)
        output_dir_str = std::string(onfc.filename());
        b_output_dir_label->label(output_dir_str.c_str());
        b_output_dir_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    }
}

// ==========================================
// 1. Simple File Type Detection via 'file' command
// ==========================================
std::string detect_file_type(const std::string& filepath) {
    // Use 'file' command for MIME detection
    std::string mime;
    std::string cmd = "file --brief --mime-type '" + filepath + "'";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), pipe)) {
            mime = std::string(buffer);
            if (!mime.empty() && mime.back() == '\n') mime.pop_back();
        }
        pclose(pipe);
    }
    if (mime == "application/pdf") return "pdf";
    if (mime == "image/vnd.djvu" || mime.find("djvu") != std::string::npos) return "djvu";
    if (mime == "application/vnd.openxmlformats-officedocument.wordprocessingml.document" ||
        mime == "application/epub+zip" || mime == "application/zip") {
        if (mime.find("wordprocessingml.document") != std::string::npos) return "docx";
        else return "zip_container";
    }
    if (mime == "application/msword") return "doc_legacy";
    return "unknown";
}

// ==========================================
// 2. Extraction Using System Tools Only
// ==========================================
void extract_pdf_images(const std::string& filepath, const std::string& output_folder) {
    std::cout << "[PDF] Extracting images from " << filepath << "..." << std::endl;
    mkdir(output_folder.c_str(), 0777);
    std::string prefix = output_folder + "/img";
    std::string cmd = "pdfimages -all '" + filepath + "' '" + prefix + "' > /dev/null 2>&1";
    system(cmd.c_str());
}

void extract_djvu_images(const std::string& filepath, const std::string& output_folder) {
    std::cout << "[DJVU] Rendering pages from " << filepath << "..." << std::endl;
    mkdir(output_folder.c_str(), 0777);
    // Get page count
    int pages = 0;
    {
        std::string cmd = "djvused -e n '" + filepath + "'";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            char buffer[32];
            if (fgets(buffer, sizeof(buffer), pipe)) {
                pages = atoi(buffer);
            }
            pclose(pipe);
        }
    }
    if (pages <= 0) {
        std::cout << "   -> Failed to get page count (djvused missing?)" << std::endl;
        return;
    }
    std::cout << "   -> Rendering " << pages << " pages sequentially..." << std::endl;
    for (int page = 1; page <= pages; ++page) {
        char out_tiff[256];
        snprintf(out_tiff, sizeof(out_tiff), "%s/page_%04d.tif", output_folder.c_str(), page);
        std::string cmd = "ddjvu -format=tiff -page=" + std::to_string(page) + " '" + filepath + "' '" + out_tiff + "' > /dev/null 2>&1";
        system(cmd.c_str());
        if (page % 10 == 0 || page == pages) {
            std::cout << "      -> Rendered " << page << "/" << pages << std::endl;
        }
    }
}

void extract_zip_container(const std::string& filepath, const std::string& output_folder) {
    std::cout << "[ZIP/DOCX/EPUB] Extracting media from " << filepath << "..." << std::endl;
    mkdir(output_folder.c_str(), 0777);
    std::string cmd = "unzip -j -o '" + filepath + "' '*.[pP][nN][gG]' '*.[jJ][pP][gG]' '*.[jJ][pP][eE][gG]' '*.[gG][iI][fF]' '*.[bB][mM][pP]' '*.[tT][iI][fF]*' '*.[sS][vV][gG]' '*.[wW][mM][fF]' '*.[eE][mM][fF]' -x '*/thumbnail*' -d '" + output_folder + "' > /dev/null 2>&1";
    system(cmd.c_str());
}

void convert_and_extract_legacy_doc(const std::string& filepath, const std::string& output_folder) {
    std::cout << "[DOC] Converting legacy .doc -> .docx -> extract..." << std::endl;
    std::string temp_dir = output_folder + "/_temp_doc";
    mkdir(temp_dir.c_str(), 0777);
    std::string cmd = "soffice --headless --convert-to docx --outdir '" + temp_dir + "' '" + filepath + "' > /dev/null 2>&1";
    system(cmd.c_str());
    // Find .docx file in temp_dir
    std::string docx_found;
    DIR* dir = opendir(temp_dir.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string fname = entry->d_name;
            if (fname.size() > 5 && fname.substr(fname.size()-5) == ".docx") {
                docx_found = temp_dir + "/" + fname;
                break;
            }
        }
        closedir(dir);
    }
    if (!docx_found.empty()) {
        extract_zip_container(docx_found, output_folder);
    } else {
        std::cout << "   -> Conversion failed: no .docx produced" << std::endl;
    }
    std::string rm_cmd = "rm -rf '" + temp_dir + "'";
    system(rm_cmd.c_str());
}

// ==========================================
// 3. Main Router
// ==========================================
void process_document(const std::string& filepath, const std::string& output_root) {
    struct stat st;
    if (stat(filepath.c_str(), &st) != 0) {
        std::cout << "File not found: " << filepath << std::endl;
        return;
    }
    std::string ftype = detect_file_type(filepath);
    std::string basename = filepath.substr(filepath.find_last_of("/\\") + 1);
    size_t dot = basename.find_last_of('.');
    if (dot != std::string::npos) basename = basename.substr(0, dot);
    std::string target_folder = output_root + "/" + basename;
    std::cout << "\nProcessing: " << filepath << " -> " << ftype << std::endl;
    if (ftype == "pdf") {
        extract_pdf_images(filepath, target_folder);
    } else if (ftype == "djvu") {
        extract_djvu_images(filepath, target_folder);
    } else if (ftype == "docx" || ftype == "zip_container") {
        extract_zip_container(filepath, target_folder);
    } else if (ftype == "doc_legacy") {
        convert_and_extract_legacy_doc(filepath, target_folder);
    } else {
        std::cout << "   -> Unsupported or unknown type: " << ftype << std::endl;
    }
}

static void start_cb (Fl_Widget* o) {
    // Step 1: check if we have files and output path
    if ((input_files_vec.size() == 0) | (output_dir_str.empty())) {
        return;
    }
    b_input_files->deactivate();
    b_output_dir->deactivate();
    startb->deactivate(); // New: Deactivate start button
    progress_bar->show(); // New: Show progress bar
    progress_bar->value(0); // New: Reset progress bar

    float progress_step = 100.0f / input_files_vec.size(); // New: Calculate progress step
    int files_processed = 0; // New: Counter for processed files

    // Step 2 + 3: check if we support the file in the vector and process it.
    for (const auto& path : input_files_vec) {
        std::string ftype = detect_file_type(path);
        bool supported = false;
        if (ftype == "pdf" && support_PDF) supported = true;
        else if (ftype == "djvu" && support_DJVU) supported = true;
        else if ((ftype == "docx" || ftype == "doc_legacy") && support_DOC) supported = true;
        else if ((ftype == "zip_container" || ftype == "epub") && support_EPUB) supported = true;
        if (!supported) {
            std::cout << "Skipping unsupported file: " << path << " (type: " << ftype << ")" << std::endl;
            continue;
        }
        process_document(path, output_dir_str);
        files_processed++; // New: Increment processed files
        progress_bar->value(files_processed * progress_step); // New: Update progress bar
        Fl::check(); // New: Process FLTK events to update GUI
    }
    b_input_files->activate();
    b_output_dir->activate();
    startb->activate(); // New: Activate start button
    progress_bar->hide(); // New: Hide progress bar
}


int main(int argc, char **argv) {
    
    wstart = new Fl_Double_Window(600, 256);
    
    {
        l = new Fl_Text_Display(5, 5, 590, 196);
        Fl_Text_Buffer* buff = new Fl_Text_Buffer();
        l->buffer(buff);
        
        Fl::add_timeout(0, check_deps);
        
        bc = new Fl_Button(80, 196+10, 74, 32, "Continue");
        bc->hide();
        bc->callback(bc_cb);
        ba = new Fl_Button(5, 196+10, 64, 32, "Abort");
        ba->callback(quit_cb);
    }
    wstart->end();

    wmain = new Fl_Double_Window(512, 256);
    {
        new Fl_Box(50, 20, 200, 10, "Choose documents to extract images from.");
        b_input_files = new Fl_Button(10, 40, 128, 32, "Choose");
        b_input_files->callback(input_docs_cb);
        b_input_files_count = new Fl_Box(148, 48, 96, 24);
        new Fl_Box(30, 88, 120, 10, "Choose output directory.");
        b_output_dir = new Fl_Button(10, 108, 128, 32, "Choose");
        b_output_dir->callback(output_dir_cb);
        b_output_dir_label = new Fl_Box(148, 112, 356, 24);

        Fl_Button* quitb = new Fl_Button(512-74, 256-42, 64, 32, "Exit");
        quitb->callback(quit_cb);

        startb = new Fl_Button(512-74, 10, 64, 32, "Start");
        startb->callback(start_cb);

        progress_bar = new Fl_Progress(10, 220, 492, 24); // New: Progress bar
        progress_bar->minimum(0);
        progress_bar->maximum(100);
        progress_bar->value(0);
        progress_bar->hide();
    }
    wmain->end();

    wstart->show(argc, argv);

    return Fl::run();
};
