#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "stb_image.h"
#include "tinyfiledialogs.h"

struct Image {
    GLuint texture;
    int width;
    int height;
    float zoom;
    ImVec2 position;
    ImVec2 targetPosition;
    std::string name;
    bool open;
    bool selected;
    bool mirrored;
    int uploadOrder;
    std::vector<unsigned char> data;
    bool eraserMode;
    int eraserSize;
    float rotation; 
    float targetRotation; // New member for rotation angle
};

std::vector<Image> images;
bool show_metrics = false;
int nextUploadOrder = 0;


struct ImageState {
    std::vector<Image> images;
    int nextUploadOrder;
};

std::vector<ImageState> undoStates;
std::vector<ImageState> redoStates;

GLuint CreateTextureFromData(const std::vector<unsigned char>& data, int width, int height)
{
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    return texture;
}

void LoadTextureFromFile(const char* filename, Image& img)
{
    std::cout << "Loading image: " << filename << std::endl;
    int channels;
    unsigned char* image = stbi_load(filename, &img.width, &img.height, &channels, 4);
    if (image == nullptr)
    {
        std::cerr << "Failed to load image" << std::endl;
        return;
    }

    img.data.assign(image, image + img.width * img.height * 4);
    stbi_image_free(image);

    img.texture = CreateTextureFromData(img.data, img.width, img.height);

    std::cout << "Image loaded successfully. Width: " << img.width << ", Height: " << img.height << std::endl;
}

bool IsPointInImage(const Image& img, const ImVec2& point)
{
    ImVec2 topLeft = img.position;
    ImVec2 bottomRight = ImVec2(img.position.x + img.width * img.zoom, img.position.y + img.height * img.zoom);
    return point.x >= topLeft.x && point.x <= bottomRight.x && point.y >= topLeft.y && point.y <= bottomRight.y;
}

void EraseImagePart(Image& img, const ImVec2& point)
{
    // Calculate the center of the image
    ImVec2 center = ImVec2(img.position.x + img.width * img.zoom * 0.5f, 
                           img.position.y + img.height * img.zoom * 0.5f);

    // Translate point to origin
    ImVec2 translated = ImVec2(point.x - center.x, point.y - center.y);

    // Rotate point back
    float cos_r = cosf(-img.rotation * 3.14159f / 180.0f); // Note the negative rotation
    float sin_r = sinf(-img.rotation * 3.14159f / 180.0f);
    ImVec2 rotated = ImVec2(
        translated.x * cos_r - translated.y * sin_r,
        translated.x * sin_r + translated.y * cos_r
    );

    // Scale back to image coordinates
    int centerX = static_cast<int>((rotated.x / img.zoom) + img.width * 0.5f);
    int centerY = static_cast<int>((rotated.y / img.zoom) + img.height * 0.5f);

    for (int y = -img.eraserSize; y <= img.eraserSize; ++y)
    {
        for (int x = -img.eraserSize; x <= img.eraserSize; ++x)
        {
            // Check if the point is within the circular eraser area
            if (x*x + y*y <= img.eraserSize*img.eraserSize)
            {
                int pixelX = centerX + x;
                int pixelY = centerY + y;
                
                // Apply mirroring if necessary
                if (img.mirrored)
                {
                    pixelX = img.width - 1 - pixelX;
                }

                if (pixelX >= 0 && pixelX < img.width && pixelY >= 0 && pixelY < img.height)
                {
                    int index = (pixelY * img.width + pixelX) * 4;
                    img.data[index + 3] = 0; // Set alpha to 0 (transparent)
                }
            }
        }
    }

    // Update texture
    glBindTexture(GL_TEXTURE_2D, img.texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img.width, img.height, GL_RGBA, GL_UNSIGNED_BYTE, img.data.data());
}


Image CreateImageCopy(const Image& original)
{
    Image copy = original;
    copy.texture = CreateTextureFromData(copy.data, copy.width, copy.height);
    copy.position.x += 20;  // Offset the copy slightly
    copy.position.y += 20;
    copy.targetPosition = copy.position;
    copy.uploadOrder = nextUploadOrder++;
    copy.selected = false;  // The new copy is not selected initially
    return copy;
}

void ShowControlPanel(Image* selectedImage)
{
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 200, 0));
    ImGui::SetNextWindowSize(ImVec2(200, ImGui::GetIO().DisplaySize.y));
    ImGui::Begin("Control Panel", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    if (selectedImage)
    {
        ImGui::Text("Selected Image: %s", selectedImage->name.c_str());

        ImGui::SetNextItemWidth(120);
        if (ImGui::SliderFloat("Zoom", &selectedImage->zoom, 0.1f, 5.0f, "%.2fx"))
        {
            undoStates.push_back({images, nextUploadOrder});
            redoStates.clear();
            selectedImage->selected = true;
        }

        if (!selectedImage->mirrored)
        {
            if (ImGui::Button("Mirror"))
            {
                undoStates.push_back({images, nextUploadOrder});
                redoStates.clear();
                selectedImage->mirrored = true;
                selectedImage->selected = true;
            }
        }
        else
        {
            if (ImGui::Button("Reset Mirror"))
            {
                undoStates.push_back({images, nextUploadOrder});
                redoStates.clear();
                selectedImage->mirrored = false;
                selectedImage->selected = true;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button(selectedImage->eraserMode ? "Move" : "Eraser"))
        {
            undoStates.push_back({images, nextUploadOrder});
            redoStates.clear();
            selectedImage->eraserMode = !selectedImage->eraserMode;
            selectedImage->selected = true;
        }

        if (selectedImage->eraserMode)
        {
            ImGui::SetNextItemWidth(120);
            if (ImGui::SliderInt("Eraser Size", &selectedImage->eraserSize, 1, 20))
            {
                undoStates.push_back({images, nextUploadOrder});
                redoStates.clear();
                selectedImage->selected = true;
            }
        }

        ImGui::SetNextItemWidth(120);
        float tempRotation = selectedImage->rotation;
        if (ImGui::SliderFloat("Rotate", &tempRotation, 0.0f, 360.0f, "%.1f°"))
        {
            undoStates.push_back({images, nextUploadOrder});
            redoStates.clear();
            float rotationDiff = tempRotation - selectedImage->rotation;
            selectedImage->rotation += rotationDiff * 0.01f;
            
            if (selectedImage->rotation < 0.0f) selectedImage->rotation += 360.0f;
            if (selectedImage->rotation >= 360.0f) selectedImage->rotation -= 360.0f;
            
            selectedImage->selected = true;
        }

        if (ImGui::Button("Copy"))
        {
            undoStates.push_back({images, nextUploadOrder});
            redoStates.clear();
            Image copy = CreateImageCopy(*selectedImage);
            images.push_back(copy);
            selectedImage->selected = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Delete"))
        {
            undoStates.push_back({images, nextUploadOrder});
            redoStates.clear();
            selectedImage->open = false;
            selectedImage->selected = true;
        }

        if (ImGui::Button("Move to Back"))
        {
            undoStates.push_back({images, nextUploadOrder});
            redoStates.clear();
            int lowestOrder = std::numeric_limits<int>::max();
            for (const auto& img : images)
            {
                if (img.uploadOrder < lowestOrder)
                {
                    lowestOrder = img.uploadOrder;
                }
            }
            selectedImage->uploadOrder = lowestOrder - 1;
        }
    }
    else
    {
        ImGui::Text("No image selected");
    }

    ImGui::End();
}



void DisplayImage(Image& img, bool& imageClicked)
{
    // Smooth movement
    img.position.x = img.position.x * 0.9f + img.targetPosition.x * 0.1f;
    img.position.y = img.position.y * 0.9f + img.targetPosition.y * 0.1f;

    ImVec2 uv_min = img.mirrored ? ImVec2(1.0f, 0.0f) : ImVec2(0.0f, 0.0f);
    ImVec2 uv_max = img.mirrored ? ImVec2(0.0f, 1.0f) : ImVec2(1.0f, 1.0f);
    ImVec2 scaled_size = ImVec2(img.width * img.zoom, img.height * img.zoom);

    // Calculate the center of the image
    ImVec2 center = ImVec2(img.position.x + scaled_size.x * 0.5f, img.position.y + scaled_size.y * 0.5f);

    // Calculate rotated corners
    ImVec2 corners[4] = {
        ImVec2(-scaled_size.x * 0.5f, -scaled_size.y * 0.5f),
        ImVec2(scaled_size.x * 0.5f, -scaled_size.y * 0.5f),
        ImVec2(scaled_size.x * 0.5f, scaled_size.y * 0.5f),
        ImVec2(-scaled_size.x * 0.5f, scaled_size.y * 0.5f)
    };

    float cos_r = cosf(img.rotation * 3.14159f / 180.0f);
    float sin_r = sinf(img.rotation * 3.14159f / 180.0f);
    for (int i = 0; i < 4; ++i)
    {
        ImVec2 rotated = ImVec2(
            corners[i].x * cos_r - corners[i].y * sin_r + center.x,
            corners[i].x * sin_r + corners[i].y * cos_r + center.y
        );
        corners[i] = rotated;
    }

    // Find the top-left and bottom-right corners of the bounding box
    ImVec2 topLeft = corners[0], bottomRight = corners[0];
    for (int i = 1; i < 4; ++i)
    {
        topLeft.x = std::min(topLeft.x, corners[i].x);
        topLeft.y = std::min(topLeft.y, corners[i].y);
        bottomRight.x = std::max(bottomRight.x, corners[i].x);
        bottomRight.y = std::max(bottomRight.y, corners[i].y);
    }

    // Add padding to the bounding box
    float padding = 15.0f;
    topLeft.x -= padding;
    topLeft.y -= padding;
    bottomRight.x += padding;
    bottomRight.y += padding;

    // Draw the image
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddImageQuad(
        (void*)(intptr_t)img.texture,
        corners[0], corners[1], corners[2], corners[3],
        uv_min, ImVec2(uv_max.x, uv_min.y), uv_max, ImVec2(uv_min.x, uv_max.y)
    );

    // Custom hit-testing and interaction logic
    ImVec2 mousePos = ImGui::GetMousePos();
    bool isHovered = mousePos.x >= topLeft.x && mousePos.x <= bottomRight.x &&
                     mousePos.y >= topLeft.y && mousePos.y <= bottomRight.y;

    static Image* draggedImage = nullptr;
    static ImVec2 dragStartPos;
    static ImVec2 imageDragStartPos;

    if (isHovered && ImGui::IsMouseClicked(0) && !imageClicked)
    {
        img.selected = true;
        imageClicked = true;
        for (auto& other_img : images) {
            if (&other_img != &img) {
                other_img.selected = false;
            }
        }
        draggedImage = &img;
        dragStartPos = ImGui::GetMousePos();
        imageDragStartPos = img.position;
    }

    if (ImGui::IsMouseReleased(0))
    {
        draggedImage = nullptr;
    }

    if (draggedImage == &img && ImGui::IsMouseDown(0))
    {
        if (img.eraserMode)
        {
            EraseImagePart(img, mousePos);
        }
        else
        {
            ImVec2 dragDelta = ImVec2(ImGui::GetMousePos().x - dragStartPos.x,
                                      ImGui::GetMousePos().y - dragStartPos.y);
            
            float smoothFactor = 1.0f;  // Direct movement
            img.targetPosition.x = imageDragStartPos.x + dragDelta.x * smoothFactor;
            img.targetPosition.y = imageDragStartPos.y + dragDelta.y * smoothFactor;
        }
    }

    // Draw selection box around the selected image
    if (img.selected)
    {
        draw_list->AddRect(
            topLeft,
            bottomRight,
            IM_COL32(180, 190, 254, 255), 0.0f, 15, 2.0f
        );
    }
}



void ShowImageViewer(bool* p_open)
{
    static Image* selectedImage = nullptr;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x - 200, ImGui::GetIO().DisplaySize.y));
    ImGui::Begin("Image Viewer", p_open, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

    ImGui::Text("Welcome to the Advanced Image Viewer!");

    if (ImGui::Button("Load Image"))
    {
        std::cout << "Load Image button clicked" << std::endl;
        const char* filters[] = { "*.png", "*.jpg", "*.jpeg", "*.bmp" };
        const char* file = tinyfd_openFileDialog(
            "Open Image",
            "",
            4,
            filters,
            "Image Files",
            0
        );
        if (file)
        {
            std::cout << "File selected: " << file << std::endl;
            Image img;
            LoadTextureFromFile(file, img);
            if (img.texture)
            {
                // Save state for undo
                undoStates.push_back({images, nextUploadOrder});
                redoStates.clear();

                img.zoom = 1.0f;
                img.position = img.targetPosition = ImVec2(50, 50);
                img.name = file;
                img.open = true;
                img.selected = false;
                img.mirrored = false;
                img.uploadOrder = nextUploadOrder++;
                img.eraserMode = false;
                img.eraserSize = 5;
                img.rotation = 0.0f;
                img.targetRotation = 0.0f;
                images.push_back(img);
                std::cout << "Image added to the viewer" << std::endl;
            }
            else
            {
                std::cerr << "Failed to load image texture" << std::endl;
            }
        }
        else
        {
            std::cerr << "No file selected or dialog cancelled" << std::endl;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear All"))
    {
        // Save current state for undo
        undoStates.push_back({images, nextUploadOrder});
        redoStates.clear();

        std::cout << "Clear All button clicked" << std::endl;
        for (const auto& img : images)
        {
            glDeleteTextures(1, &img.texture);
        }
        images.clear();
        nextUploadOrder = 0;
        selectedImage = nullptr;
    }

    ImGui::SameLine();
    if (ImGui::Button("Undo") && !undoStates.empty())
    {
        // Save current state for redo
        redoStates.push_back({images, nextUploadOrder});

        // Restore previous state
        ImageState prevState = undoStates.back();
        undoStates.pop_back();

        // Clear current images
        for (const auto& img : images)
        {
            glDeleteTextures(1, &img.texture);
        }

        // Restore images and nextUploadOrder
        images = prevState.images;
        nextUploadOrder = prevState.nextUploadOrder;

        // Recreate textures for restored images
        for (auto& img : images)
        {
            img.texture = CreateTextureFromData(img.data, img.width, img.height);
        }

        selectedImage = nullptr;
    }

    ImGui::SameLine();
    if (ImGui::Button("Redo") && !redoStates.empty())
    {
        // Save current state for undo
        undoStates.push_back({images, nextUploadOrder});

        // Restore next state
        ImageState nextState = redoStates.back();
        redoStates.pop_back();

        // Clear current images
        for (const auto& img : images)
        {
            glDeleteTextures(1, &img.texture);
        }

        // Restore images and nextUploadOrder
        images = nextState.images;
        nextUploadOrder = nextState.nextUploadOrder;

        // Recreate textures for restored images
        for (auto& img : images)
        {
            img.texture = CreateTextureFromData(img.data, img.width, img.height);
        }

        selectedImage = nullptr;
    }

    ImGui::SameLine();
    ImGui::Checkbox("Show Metrics", &show_metrics);

    ImGui::BeginChild("ImageDisplayArea", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    
    // Sort images based on upload order (ascending)
    std::sort(images.begin(), images.end(), [](const Image& a, const Image& b) {
        return a.uploadOrder < b.uploadOrder;
    });

    ImVec2 mousePos = ImGui::GetMousePos();
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 relativeMousePos = ImVec2(mousePos.x - windowPos.x, mousePos.y - windowPos.y);

    Image* hoveredImage = nullptr;
    bool imageAreaClicked = false;

    // Display all images and find the topmost hovered image
    for (auto& img : images)
    {
        if (img.open)
        {
            bool dummy = false;
            DisplayImage(img, dummy);

            if (IsPointInImage(img, relativeMousePos))
            {
                hoveredImage = &img;
                imageAreaClicked = true;
            }
        }
    }

    // Handle selection and deselection
    if (ImGui::IsMouseClicked(0))
    {
        if (hoveredImage)
        {
            selectedImage = hoveredImage;
            for (auto& img : images)
            {
                img.selected = (&img == selectedImage);
            }
        }
        else if (!imageAreaClicked && ImGui::IsWindowHovered())
        {
            // Deselect all images when clicking on empty space
            selectedImage = nullptr;
            for (auto& img : images)
            {
                img.selected = false;
            }
        }
    }

    images.erase(std::remove_if(images.begin(), images.end(),
        [&](const Image& img) { 
            if (!img.open) {
                if (&img == selectedImage) {
                    selectedImage = nullptr;
                }
                glDeleteTextures(1, &img.texture);
                return true;
            }
            return false;
        }), images.end());

    ImGui::EndChild();

    ImGui::End();

    // Show the control panel for the selected image
    ShowControlPanel(selectedImage);

    if (show_metrics)
    {
        ImGui::ShowMetricsWindow(&show_metrics);
    }
}

int main(int, char**)
{
    std::cout << "Starting application..." << std::endl;

    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    std::cout << "GLFW initialized successfully" << std::endl;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Advanced Image Viewer", NULL, NULL);
    if (window == NULL)
    {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return 1;
    }

    std::cout << "GLFW window created successfully" << std::endl;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    std::cout << "OpenGL context created" << std::endl;

    const GLubyte* version = glGetString(GL_VERSION);
    std::cout << "OpenGL Version: " << (version ? (char*)version : "Unknown") << std::endl;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    std::cout << "ImGui context created" << std::endl;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    if (!ImGui_ImplOpenGL3_Init("#version 120"))
    {
        std::cerr << "Failed to initialize ImGui OpenGL3 implementation" << std::endl;
        return 1;
    }

    std::cout << "ImGui initialized for OpenGL" << std::endl;

    bool show_viewer = true;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ShowImageViewer(&show_viewer);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    std::cout << "Cleaning up..." << std::endl;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "Application closed successfully" << std::endl;

    return 0;
}



