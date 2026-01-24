#include <iostream>
#include <string>
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Check_Button.H>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <vector>
#include <sys/stat.h>
#include <dirent.h>
#include <sstream>
#include <cmath>
#include <algorithm> // For std::min, std::max
#include <iomanip>   // For std::setw, std::setfill

// Threading Includes
#include <thread>
#include <atomic>
#include <mutex>
#include <future>

// Prevent X11 Status conflict
#define Status Status_
#include <opencv2/opencv.hpp>
#undef Status

// Tesseract Includes
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

bool support_DJVU = true;
bool support_PDF = true;
bool support_PDF_RENDER = false; 
bool support_DOC = true;
bool support_EPUB = true;
bool support_OPENCV = false;
bool support_TESSERACT = false;

// ==========================================
// Global UI Pointers
// ==========================================
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
Fl_Progress* progress_bar = nullptr;
Fl_Button* startb = nullptr;

Fl_Check_Button* opencv_toggle = nullptr;
Fl_Check_Button* tesseract_toggle = nullptr;
Fl_Check_Button* multithread_toggle = nullptr;

Fl_Box* status_box = nullptr;

std::string input_files_count_str;
std::vector<std::string> input_files_vec;
std::string output_dir_str;

// ==========================================
// Global Progress State (Thread Safe)
// ==========================================
int g_total_work_units = 0;
std::atomic<int> g_processed_work_units{0};
int g_current_file_index = 0;
bool g_use_multithreading = true; // Default ON

// Thread limits to prevent system overload on WSL
const int MAX_RENDER_THREADS = std::max(2, (int)std::thread::hardware_concurrency());
const int MAX_CV_THREADS = std::max(2, (int)std::thread::hardware_concurrency());

static void clear_status_cb(void*) {
    if (status_box) {
        status_box->label("");
        status_box->redraw();
    }
}

int call(std::string c) {
    std::string buffer;
    buffer.append(c);
    buffer.append(" > /dev/null 2>&1");
    return system(buffer.c_str());
};

// ==========================================
// Dependency Checking
// ==========================================
int check_dependencies() {
    int dep_count = 0;

    // Check: ddjvu
    {
        int res = call("ddjvu --help");
        if (res == 32512) {
            l->insert("Command not found: ddjvu\n");
            support_DJVU = false;
        }
        else if (res == 256) dep_count++;
        else support_DJVU = false;
    }

    // Check: djvused
    {
        int res = call("djvused --help");
        if (res == 32512) support_DJVU = false;
        else if (res == 2560) dep_count++;
        else support_DJVU = false;
    }

    // Check: soffice
    {
        int res = call("soffice --version");
        if (res == 32512) {
            support_DOC = false; support_EPUB = false;
        }
        else if (res == 0) dep_count++;
        else {
            support_DOC = false; support_EPUB = false;
        }
    }

    // Check: pdfimages
    {
        int res = call("pdfimages -v");
        if (res == 32512) support_PDF = false;
        else if (res == 0) dep_count++;
        else support_PDF = false;
    }

    // Check: unzip
    {
        int res = call("unzip");
        if (res == 32512) {
            support_DOC = false; support_EPUB = false;
        } else {
            dep_count++;
        }
    }

    // Check: pdftoppm
    {
        int res = call("pdftoppm -v");
        if (res == 0) {
            support_PDF_RENDER = true;
        } else {
            l->insert("pdftoppm not found - Cannot render PDF pages for vector figure detection\n");
        }
    }

    // Check: OpenCV
    {
        int res = call("pkg-config --exists opencv4");
        if (res == 0) {
            support_OPENCV = true;
            l->insert("OpenCV found\n");
        } else {
            l->insert("OpenCV not found - advanced figure extraction disabled\n");
            if (opencv_toggle) {
                opencv_toggle->deactivate();
                opencv_toggle->value(0);
            }
        }
    }

    // Check: Tesseract
    {
        int res = call("tesseract --version");
        if (res == 0) {
            support_TESSERACT = true;
            l->insert("Tesseract found - OCR figure extraction available\n");
        } else {
            l->insert("Tesseract not found - OCR check disabled\n");
            if (tesseract_toggle) {
                tesseract_toggle->deactivate();
                tesseract_toggle->value(0);
            }
        }
    }

    return dep_count;
}

void check_deps(void* user_data) {
    l->insert("Checking dependencies...\n");
    int dep_count = check_dependencies();
    
    std::string msg = std::string("Base Dependencies: ") + std::to_string(dep_count) + "/" + std::to_string(5) + "\n";
    l->insert(msg.c_str());

    if (dep_count == 0) {
        l->insert("No dependencies found. Install and restart.\n");
    } else if (dep_count < 5) {
        l->insert("Some dependencies missing. Continue?\n");
        bc->show();
    } else {
        l->insert("All base dependencies found!\n");
        wstart->hide();
        wmain->show();
    }
}

// ==========================================
// OpenCV & Tesseract Logic
// ==========================================

struct FigureCandidate {
    cv::Rect bbox;
    double textDensity;
    double area;
};

double calculateTextDensity(const cv::Mat& region) {
    cv::Mat gray, binary;
    if (region.channels() == 3) {
        cv::cvtColor(region, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = region.clone();
    }
    
    cv::adaptiveThreshold(gray, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                          cv::THRESH_BINARY_INV, 15, 10);
    
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);
    
    cv::Mat labels, stats, centroids;
    int numComponents = cv::connectedComponentsWithStats(binary, labels, stats, centroids, 8);
    
    int textLikeComponents = 0;
    for (int i = 1; i < numComponents; i++) {
        int width = stats.at<int>(i, cv::CC_STAT_WIDTH);
        int height = stats.at<int>(i, cv::CC_STAT_HEIGHT);
        int area = stats.at<int>(i, cv::CC_STAT_AREA);
        
        if (height > 5 && height < 50 && width > 3 && width < 200) {
            double aspectRatio = (double)width / height;
            if (aspectRatio > 0.2 && aspectRatio < 10 && area > 20) {
                textLikeComponents++;
            }
        }
    }
    
    return (double)textLikeComponents / (region.rows * region.cols) * 10000;
}

bool hasGraphicalContent(const cv::Mat& region) {
    cv::Mat gray, edges;
    if (region.channels() == 3) {
        cv::cvtColor(region, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = region.clone();
    }
    
    cv::Canny(gray, edges, 50, 150);
    int edgePixels = cv::countNonZero(edges);
    double edgeDensity = (double)edgePixels / (region.rows * region.cols);
    
    return edgeDensity > 0.005 && edgeDensity < 0.15;
}

int countWords(const std::string& text) {
    std::stringstream ss(text);
    std::string word;
    int count = 0;
    while (ss >> word) count++;
    return count;
}

bool isTextBlock(tesseract::TessBaseAPI* tess, const cv::Mat& region, std::string& extractedTextOut) {
    if (!tess) return false;

    cv::Mat gray;
    if (region.channels() == 3) {
        cv::cvtColor(region, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = region;
    }

    tess->SetImage(gray.data, gray.cols, gray.rows, 1, gray.step);
    
    int conf = tess->MeanTextConf();
    char* text = tess->GetUTF8Text();
    std::string strText(text);
    delete[] text; 
    
    extractedTextOut = strText;
    int wordCount = countWords(strText);
    
    if (conf > 70 && wordCount > 25) {
        return true;
    }
    
    return false;
}

bool isLikelyPureTextCV(double textDensity) {
    if (textDensity > 10.0) return true; 
    return false;
}

std::vector<cv::Rect> extractFigures(const cv::Mat& image, tesseract::TessBaseAPI* tess, bool useOCR) {
    std::vector<cv::Rect> figures;
    
    cv::Mat gray, binary;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    
    cv::adaptiveThreshold(gray, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                          cv::THRESH_BINARY_INV, 25, 15);
    
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::dilate(binary, binary, kernel, cv::Point(-1, -1), 3);
    
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    std::vector<FigureCandidate> candidates;
    
    double minArea = image.rows * image.cols * 0.01;
    double maxArea = image.rows * image.cols * 0.7;
    
    for (const auto& contour : contours) {
        cv::Rect bbox = cv::boundingRect(contour);
        double area = bbox.width * bbox.height;
        
        if (area < minArea || area > maxArea) continue;
        if (bbox.width < 100 || bbox.height < 100) continue;
        
        int padX = (int)(bbox.width * 0.05);
        int padY = (int)(bbox.height * 0.05);

        int x = bbox.x - padX;
        int y = bbox.y - padY;

        if (x < 0) x = 0;
        if (y < 0) y = 0;

        int width = bbox.width + 2 * padX;
        int height = bbox.height + 2 * padY;

        if (x + width > image.cols) width = image.cols - x;
        if (y + height > image.rows) height = image.rows - y;
        
        cv::Rect paddedBox(x, y, width, height);
        cv::Mat region = image(paddedBox);
        double textDensity = calculateTextDensity(region);
        
        candidates.push_back({paddedBox, textDensity, area});
    }
    
    std::sort(candidates.begin(), candidates.end(),
         [](const FigureCandidate& a, const FigureCandidate& b) {
             return a.area > b.area;
         });
    
    for (const auto& candidate : candidates) {
        cv::Mat region = image(candidate.bbox);
        bool hasGraphics = hasGraphicalContent(region);
        
        if (candidate.textDensity > 20.0) {
            if (hasGraphics) {
                figures.push_back(candidate.bbox);
            }
            continue;
        }

        if (candidate.textDensity < 2.0) {
            figures.push_back(candidate.bbox);
            continue;
        }

        if (isLikelyPureTextCV(candidate.textDensity)) {
            continue; 
        }

        std::string ocrTextResult = "";
        bool isTextBlockByOCR = false;
        
        if (useOCR && tess) {
             isTextBlockByOCR = isTextBlock(tess, region, ocrTextResult);
        }

        if (isTextBlockByOCR) {
            continue; 
        }
        
        if (hasGraphics) {
            figures.push_back(candidate.bbox);
        }
    }
    
    return figures;
}

// Process a single image file (Thread Safe)
void process_single_image(const std::string& image_path, const std::string& output_folder, bool use_tesseract) {
    // Initialize Tesseract locally for this thread
    tesseract::TessBaseAPI* tess = nullptr;
    if (use_tesseract && support_TESSERACT) {
        tess = new tesseract::TessBaseAPI();
        if (tess->Init(NULL, "eng")) {
            delete tess;
            tess = nullptr;
        } else {
            tess->SetPageSegMode(tesseract::PSM_AUTO);
        }
    }

    cv::Mat image = cv::imread(image_path, cv::IMREAD_COLOR);
    if (!image.empty()) {
        std::vector<cv::Rect> figures = extractFigures(image, tess, (tess != nullptr));

        size_t last_slash = image_path.find_last_of("/\\");
        size_t last_dot = image_path.find_last_of(".");
        std::string base_name = image_path.substr(last_slash + 1, last_dot - last_slash - 1);

        std::string opencv_folder = output_folder + "/opencv_figures";
        mkdir(opencv_folder.c_str(), 0777);

        for (size_t i = 0; i < figures.size(); i++) {
            cv::Mat figure = image(figures[i]);
            std::string output_path = opencv_folder + "/" + base_name + "_figure_" + std::to_string(i+1) + ".png";
            cv::imwrite(output_path, figure);
        }
    }

    if (tess) {
        tess->End();
        delete tess;
    }

    // Atomic Increment
    g_processed_work_units++;
}

void process_extracted_images_with_opencv(const std::string& folder_path, bool use_tesseract) {
    std::vector<std::string> image_files;
    DIR* dir;
    struct dirent* entry;

    if ((dir = opendir(folder_path.c_str())) != nullptr) {
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            if (filename.size() > 4 &&
                (filename.substr(filename.size()-4) == ".png" ||
                 filename.substr(filename.size()-4) == ".jpg" ||
                 filename.substr(filename.size()-4) == ".tif" ||
                 filename.substr(filename.size()-5) == ".jpeg")) {
                image_files.push_back(folder_path + "/" + filename);
            }
        }
        closedir(dir);
    }

    if (image_files.empty()) return;

    // Parallel vs Serial Processing Loop
    std::vector<std::thread> threads;
    int active_threads = 0;

    for (const auto& image_path : image_files) {
        if (g_use_multithreading) {
            // Limit concurrency
            while (active_threads >= MAX_CV_THREADS) {
                for (auto& t : threads) {
                    if (t.joinable()) {
                        t.join();
                        active_threads--;
                    }
                }
                threads.erase(
                    std::remove_if(threads.begin(), threads.end(), 
                        [](std::thread& t){ return !t.joinable(); }),
                    threads.end());
            }
            threads.emplace_back(process_single_image, image_path, folder_path, use_tesseract);
            active_threads++;
        } else {
            // Serial
            process_single_image(image_path, folder_path, use_tesseract);
        }
    }

    // Join remaining threads (only in parallel mode)
    if (g_use_multithreading) {
        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }
    }
}

// ==========================================
// Helper: Get Page Count
// ==========================================
int get_page_count(const std::string& filepath, const std::string& ftype) {
    if (ftype == "pdf") {
        std::string cmd = "pdfinfo \"" + filepath + "\" 2>/dev/null | grep Pages | awk '{print $2}'";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            char buffer[32];
            if (fgets(buffer, sizeof(buffer), pipe)) {
                int p = atoi(buffer);
                pclose(pipe);
                if (p > 0) return p;
            }
            pclose(pipe);
        }
        return 1; 
    }
    else if (ftype == "djvu") {
        std::string cmd = "djvused -e 'n' \"" + filepath + "\" 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            char buffer[32];
            if (fgets(buffer, sizeof(buffer), pipe)) {
                int p = atoi(buffer);
                pclose(pipe);
                if (p > 0) return p;
            }
            pclose(pipe);
        }
        return 1;
    }
    return 1; 
}

// ==========================================
// Rendering Functions (Parallelized)
// ==========================================

void render_single_pdf_page(const std::string& filepath, int page, const std::string& output_folder, const std::string& prefix) {
    std::stringstream ss;
    ss << std::setw(4) << std::setfill('0') << page;
    
    // > /dev/null 2>&1 suppresses the "Invalid resolution" warnings
    std::string cmd = "pdftoppm -f " + std::to_string(page) + " -l " + std::to_string(page) + 
                      " -png -r 200 \"" + filepath + "\" \"" + prefix + "\" > /dev/null 2>&1";
    system(cmd.c_str());

    g_processed_work_units++; // Atomic
}

void render_pdf_pages(const std::string& filepath, const std::string& output_folder, const std::string& filename_display) {
    mkdir(output_folder.c_str(), 0777);
    std::string prefix = output_folder + "/page";
    
    int pages = get_page_count(filepath, "pdf");
    
    std::vector<std::thread> threads;
    int active_threads = 0;

    for (int page = 1; page <= pages; ++page) {
        if (g_use_multithreading) {
            while (active_threads >= MAX_RENDER_THREADS) {
                for (auto& t : threads) {
                    if (t.joinable()) {
                        t.join();
                        active_threads--;
                    }
                }
                threads.erase(
                    std::remove_if(threads.begin(), threads.end(), 
                        [](std::thread& t){ return !t.joinable(); }),
                    threads.end());
            }
            threads.emplace_back(render_single_pdf_page, filepath, page, output_folder, prefix);
            active_threads++;
        } else {
            render_single_pdf_page(filepath, page, output_folder, prefix);
        }
    }

    if (g_use_multithreading) {
        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }
    }
}

void render_single_djvu_page(const std::string& filepath, int page, const std::string& output_folder) {
    std::stringstream ss;
    ss << std::setw(4) << std::setfill('0') << page;
    std::string padded = ss.str();

    std::string output_png = output_folder + "/page_" + padded + ".png";
    std::string render_cmd = "ddjvu -format=png -page=" + std::to_string(page) + " \"" + filepath + "\" \"" + output_png + "\" > /dev/null 2>&1";
    system(render_cmd.c_str());
    
    g_processed_work_units++;
}

void render_djvu_pages(const std::string& filepath, const std::string& output_folder, const std::string& filename_display) {
    mkdir(output_folder.c_str(), 0777);
    int pages = get_page_count(filepath, "djvu");
    if (pages <= 0) return;

    std::vector<std::thread> threads;
    int active_threads = 0;

    for (int page = 1; page <= pages; ++page) {
        if (g_use_multithreading) {
            while (active_threads >= MAX_RENDER_THREADS) {
                for (auto& t : threads) {
                    if (t.joinable()) {
                        t.join();
                        active_threads--;
                    }
                }
                threads.erase(
                    std::remove_if(threads.begin(), threads.end(), 
                        [](std::thread& t){ return !t.joinable(); }),
                    threads.end());
            }
            threads.emplace_back(render_single_djvu_page, filepath, page, output_folder);
            active_threads++;
        } else {
            render_single_djvu_page(filepath, page, output_folder);
        }
    }

    if (g_use_multithreading) {
        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }
    }
}

// ==========================================
// Extraction Functions
// ==========================================
void extract_pdf_images(const std::string& filepath, const std::string& output_folder) {
    mkdir(output_folder.c_str(), 0777);
    std::string prefix = output_folder + "/img";
    std::string cmd = "pdfimages -all '" + filepath + "' '" + prefix + "' > /dev/null 2>&1";
    system(cmd.c_str());
    g_processed_work_units++; 
}

void extract_djvu_images(const std::string& filepath, const std::string& output_folder) {
    mkdir(output_folder.c_str(), 0777);
    int pages = get_page_count(filepath, "djvu");
    if (pages <= 0) return;

    std::string temp_dir = output_folder + "/_djvu_temp";
    mkdir(temp_dir.c_str(), 0777);
    const int SIZE_THRESHOLD = 200;

    std::vector<std::thread> threads;
    int active_threads = 0;

    for (int page = 1; page <= pages; ++page) {
        if (g_use_multithreading) {
            while (active_threads >= MAX_RENDER_THREADS) {
                for (auto& t : threads) {
                    if (t.joinable()) {
                        t.join();
                        active_threads--;
                    }
                }
                threads.erase(
                    std::remove_if(threads.begin(), threads.end(), 
                        [](std::thread& t){ return !t.joinable(); }),
                    threads.end());
            }

            threads.emplace_back([filepath, output_folder, temp_dir, page]() {
                std::string iw44_file = temp_dir + "/page_" + std::to_string(page) + ".iw44";
                std::string extract_cmd = "djvuextract '" + filepath + "' BG44='" + iw44_file + "' -page=" + std::to_string(page) + " > /dev/null 2>&1";
                if (system(extract_cmd.c_str()) != 0) return;

                struct stat st;
                if (stat(iw44_file.c_str(), &st) != 0 || st.st_size <= SIZE_THRESHOLD) {
                    unlink(iw44_file.c_str());
                    return;
                }

                // FIX: Use 'page' for filename to ensure uniqueness without locking a counter
                std::string page_num = std::to_string(page);
                std::string padded = std::string(4 - page_num.length(), '0') + page_num;
                std::string output_png = output_folder + "/page_" + padded + ".png";
                std::string render_cmd = "ddjvu -format=png -page=" + std::to_string(page) + " '" + filepath + "' '" + output_png + "' > /dev/null 2>&1";
                system(render_cmd.c_str());

                if (stat(output_png.c_str(), &st) == 0 && st.st_size > 1000) {
                    // File valid, keep it. No counter needed.
                }
                unlink(iw44_file.c_str());
                g_processed_work_units++;
            });
            active_threads++;
        } else {
            // Serial execution for djvu extraction
            std::string iw44_file = temp_dir + "/page_" + std::to_string(page) + ".iw44";
            std::string extract_cmd = "djvuextract '" + filepath + "' BG44='" + iw44_file + "' -page=" + std::to_string(page) + " > /dev/null 2>&1";
            if (system(extract_cmd.c_str()) == 0) {
                 struct stat st;
                if (stat(iw44_file.c_str(), &st) == 0 && st.st_size > SIZE_THRESHOLD) {
                    // FIX: Use 'page' for filename
                    std::string page_num = std::to_string(page);
                    std::string padded = std::string(4 - page_num.length(), '0') + page_num;
                    std::string output_png = output_folder + "/page_" + padded + ".png";
                    std::string render_cmd = "ddjvu -format=png -page=" + std::to_string(page) + " '" + filepath + "' '" + output_png + "' > /dev/null 2>&1";
                    system(render_cmd.c_str());
                    if (stat(output_png.c_str(), &st) == 0 && st.st_size > 1000) {
                        // File valid.
                    }
                }
                unlink(iw44_file.c_str());
            }
            g_processed_work_units++;
        }
    }
    
    if (g_use_multithreading) {
        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }
    }

    std::string rmdir_cmd = "rm -rf '" + temp_dir + "'";
    system(rmdir_cmd.c_str());
}

void extract_zip_container(const std::string& filepath, const std::string& output_folder) {
    mkdir(output_folder.c_str(), 0777);
    std::string cmd = "unzip -j -o '" + filepath + "' '*.[pP][nN][gG]' '*.[jJ][pP][gG]' '*.[jJ][pP][eE][gG]' '*.[gG][iI][fF]' '*.[bB][mM][pP]' '*.[tT][iI][fF]*' '*.[sS][vV][gG]' '*.[wW][mM][fF]' '*.[eE][mM][fF]' -x '*/thumbnail*' -d '" + output_folder + "' > /dev/null 2>&1";
    system(cmd.c_str());
    g_processed_work_units++;
}

void convert_and_extract_legacy_doc(const std::string& filepath, const std::string& output_folder) {
    std::string temp_dir = output_folder + "/_temp_doc";
    mkdir(temp_dir.c_str(), 0777);
    std::string cmd = "soffice --headless --convert-to docx --outdir '" + temp_dir + "' '" + filepath + "' > /dev/null 2>&1";
    system(cmd.c_str());
    
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
    if (!docx_found.empty()) extract_zip_container(docx_found, output_folder);
    
    std::string rm_cmd = "rm -rf '" + temp_dir + "'";
    system(rm_cmd.c_str());
}

std::string detect_file_type(const std::string& filepath) {
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

    if (mime.find("opendocument") != std::string::npos ||
        mime.find("openxmlformats") != std::string::npos ||
        mime == "application/epub+zip" ||
        mime == "application/zip") {
        return "zip_container";
    }

    if (mime == "application/msword") return "doc_legacy";
    return "unknown";
}

// ==========================================
// Processing Logic
// ==========================================

void process_document(const std::string& filepath, const std::string& output_root, bool use_opencv, bool use_tesseract) {
    struct stat st;
    if (stat(filepath.c_str(), &st) != 0) return;
    
    std::string ftype = detect_file_type(filepath);
    std::string basename = filepath.substr(filepath.find_last_of("/\\") + 1);
    size_t dot = basename.rfind('.');
    if (dot != std::string::npos) basename.erase(dot);
    std::string target_folder = output_root + "/" + basename;
    mkdir(target_folder.c_str(), 0777);

    bool pages_rendered = false;

    if (use_opencv) {
        if (ftype == "pdf" && support_PDF_RENDER) {
            render_pdf_pages(filepath, target_folder, basename);
            pages_rendered = true;
        }
        else if (ftype == "djvu" && support_DJVU) {
            render_djvu_pages(filepath, target_folder, basename);
            pages_rendered = true;
        }
        else if ((support_DOC || support_EPUB) && support_PDF_RENDER) {
            std::string temp_dir = target_folder + "/_temp_pdf_convert";
            mkdir(temp_dir.c_str(), 0777);

            std::string convert_cmd = "soffice --headless --convert-to pdf --outdir '" + temp_dir + "' '" + filepath + "' > /dev/null 2>&1";
            system(convert_cmd.c_str());

            std::string converted_pdf;
            DIR* dir = opendir(temp_dir.c_str());
            if (dir) {
                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    std::string fname = entry->d_name;
                    if (fname.length() >= 4 && fname.substr(fname.length() - 4) == ".pdf") {
                        converted_pdf = temp_dir + "/" + fname;
                        break;
                    }
                }
                closedir(dir);
            }

            if (!converted_pdf.empty() && stat(converted_pdf.c_str(), &st) == 0 && st.st_size > 1000) {
                render_pdf_pages(converted_pdf, target_folder, basename);
                pages_rendered = true;
                unlink(converted_pdf.c_str());
            }

            std::string rm_temp = "rm -rf '" + temp_dir + "'";
            system(rm_temp.c_str());
        }
    }

    if (!pages_rendered) {
        if (ftype == "pdf") {
            extract_pdf_images(filepath, target_folder);
        }
        else if (ftype == "djvu") {
            extract_djvu_images(filepath, target_folder);
        }
        else if (ftype == "zip_container") {
            extract_zip_container(filepath, target_folder);
        }
        else if (ftype == "doc_legacy") {
            convert_and_extract_legacy_doc(filepath, target_folder);
        }
    }

    if (use_opencv && support_OPENCV) {
        process_extracted_images_with_opencv(target_folder, use_tesseract && support_TESSERACT);
    }
}

// ==========================================
// UI Callbacks & Threading
// ==========================================
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
    if (infc.show() == 0) {
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
    if (onfc.show() == 0) {
        output_dir_str = std::string(onfc.filename());
        b_output_dir_label->label(output_dir_str.c_str());
        b_output_dir_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    }
}

static void opencv_toggle_cb(Fl_Widget* o, void* data) {
    if (tesseract_toggle) {
        if (opencv_toggle->value()) {
            if (support_TESSERACT) {
                tesseract_toggle->activate();
            }
        } else {
            tesseract_toggle->deactivate();
            tesseract_toggle->value(0);
        }
    }
}

// Timer to update UI from Main Thread while workers run in background
void update_ui_cb(void*) {
    float pct = (float)g_processed_work_units / g_total_work_units * 100.0f;
    if (pct > 100.0f) pct = 100.0f;
    progress_bar->value(pct);
    
    progress_bar->redraw();

    if (g_processed_work_units >= g_total_work_units) {
        // Work is done
        progress_bar->hide();
        if (status_box) {
            status_box->label("Extraction completed!");
            status_box->labelcolor(FL_GREEN);
            status_box->redraw();
            Fl::add_timeout(4.0, clear_status_cb);
        }
        
        b_input_files->activate();
        b_output_dir->activate();
        startb->activate();
        opencv_toggle->activate();
        multithread_toggle->activate();
        if(opencv_toggle->value() && support_TESSERACT) tesseract_toggle->activate();
    } else {
        Fl::repeat_timeout(0.05, update_ui_cb);
    }
}

// Thread function to handle the heavy lifting
void process_files_thread() {
    for (size_t i = 0; i < input_files_vec.size(); i++) {
        g_current_file_index = (int)i;
        const std::string& path = input_files_vec[i];
        std::string ftype = detect_file_type(path);
        
        bool supported = false;
        if (ftype == "pdf" && support_PDF) supported = true;
        else if (ftype == "djvu" && support_DJVU) supported = true;
        else if ((ftype == "docx" || ftype == "doc_legacy") && support_DOC) supported = true;
        else if ((ftype == "zip_container" || ftype == "epub") && support_EPUB) supported = true;
        
        if (supported) {
            process_document(path, output_dir_str, opencv_toggle->value(), tesseract_toggle->value());
        } else {
            g_processed_work_units++;
        }
    }
}

static void start_cb (Fl_Widget* o) {
    if (input_files_vec.size() == 0) {
        status_box->label("Input files are not chosen!");
        status_box->labelcolor(FL_RED);
        status_box->redraw();
        Fl::add_timeout(4.0, clear_status_cb);
        return;
    }
    if (output_dir_str.empty()) {
        status_box->label("Output folder is not chosen!");
        status_box->labelcolor(FL_RED);
        status_box->redraw();
        Fl::add_timeout(4.0, clear_status_cb);
        return;
    }

    // Set global flags from UI
    g_use_multithreading = multithread_toggle->value();

    b_input_files->deactivate();
    b_output_dir->deactivate();
    startb->deactivate();
    opencv_toggle->deactivate();
    multithread_toggle->deactivate();
    tesseract_toggle->deactivate();
    
    progress_bar->show();
    progress_bar->value(0);

    if (status_box) {
        status_box->copy_label("Calculating workload...");
        status_box->labelcolor(FL_FOREGROUND_COLOR);
        status_box->redraw();
    }
    Fl::check();

    // Calculate Total Work Units (Synchronous, fast)
    g_total_work_units = 0;
    g_processed_work_units = 0;
    
    bool use_opencv = opencv_toggle->value();
    
    for (const auto& path : input_files_vec) {
        std::string ftype = detect_file_type(path);
        int pages = 1;
        
        if (ftype == "pdf" || ftype == "djvu") {
            pages = get_page_count(path, ftype);
        }
        
        if (use_opencv) {
            if ((ftype == "pdf" && support_PDF_RENDER) || 
                (ftype == "djvu" && support_DJVU) ||
                ((support_DOC || support_EPUB) && support_PDF_RENDER)) {
                g_total_work_units += pages * 2; // Render + Process
            } else {
                g_total_work_units += 6; // Avg extraction + processing
            }
        } else {
            if (ftype == "pdf") {
                g_total_work_units += 1; 
            } else if (ftype == "djvu") {
                g_total_work_units += pages;
            } else {
                g_total_work_units += 1;
            }
        }
    }
    
    if (g_total_work_units == 0) g_total_work_units = 1;

    // Start UI Update Loop
    Fl::add_timeout(0.05, update_ui_cb);

    // Start Background Thread
    std::thread worker(process_files_thread);
    worker.detach();
}

// ==========================================
// Main
// ==========================================
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

    wmain = new Fl_Double_Window(512, 380); // Increased height slightly to fit the new checkbox

    {
        new Fl_Box(50, 20, 200, 10, "Choose documents to extract images from.");
        b_input_files = new Fl_Button(10, 40, 128, 32, "Choose");
        b_input_files->callback(input_docs_cb);
        b_input_files_count = new Fl_Box(148, 48, 350, 24);
        
        new Fl_Box(30, 88, 120, 10, "Choose output directory.");
        b_output_dir = new Fl_Button(10, 108, 128, 32, "Choose");
        b_output_dir->callback(output_dir_cb);
        b_output_dir_label = new Fl_Box(148, 112, 356, 24);

        opencv_toggle = new Fl_Check_Button(10, 150, 300, 24, "Enable OpenCV figure extraction");
        opencv_toggle->tooltip("Uses computer vision to detect figures inside images. PDFs will be rendered.");
        opencv_toggle->value(0);
        opencv_toggle->callback(opencv_toggle_cb);

        tesseract_toggle = new Fl_Check_Button(30, 175, 300, 24, "Use OCR (Tesseract) for verification");
        tesseract_toggle->tooltip("More accurate detection, but slower. Requires OpenCV to be enabled.");
        tesseract_toggle->value(0);
        tesseract_toggle->deactivate();

        multithread_toggle = new Fl_Check_Button(10, 200, 300, 24, "Enable Multithreading");
        multithread_toggle->tooltip("Use all CPU cores for rendering and processing. Disable if system is unstable.");
        multithread_toggle->value(1); // Default ON

        Fl_Button* quitb = new Fl_Button(512-74, 380-42, 64, 32, "Exit");
        quitb->callback(quit_cb);

        startb = new Fl_Button(512-74, 10, 64, 32, "Start");
        startb->callback(start_cb);

        progress_bar = new Fl_Progress(10, 240, 492, 24);
        progress_bar->minimum(0);
        progress_bar->maximum(100);
        progress_bar->value(0);
        progress_bar->hide();

        status_box = new Fl_Box(10, 270, 492, 24, "");
        status_box->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
        status_box->labelfont(FL_BOLD);
        status_box->labelsize(16);
    }
    wmain->end();

    wstart->show(argc, argv);

    return Fl::run();
}