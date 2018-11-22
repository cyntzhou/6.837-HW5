#include "gl.h"
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <vector>
#include <lodepng.h>
#include <map>
#include <cstdint>

#include "objparser.h"

// some utility code is tucked away in main.h
// for example, drawing the coordinate axes
// or helpers for setting uniforms.
#include "main.h"

// 4096x4096 is a pretty large texture. Extensions to shadow algorithm
// (extra credit) help lowering this memory footprint.
const int SHADOW_WIDTH = 4096;
const int SHADOW_HEIGHT = 4096;

// FUNCTION DECLARATIONS - you will implement these
void loadTextures();
void freeTextures();

void loadFramebuffer();
void freeFramebuffer();

void draw();

Matrix4f getLightView();
Matrix4f getLightProjection();

// Globals here.
objparser scene;
Vector3f  light_dir;
glfwtimer timer;


// FUNCTION IMPLEMENTATIONS

// animate light source direction
// this one is implemented for you
void updateLightDirection() {
    // feel free to edit this
    float elapsed_s = timer.elapsed();
    //elapsed_s = 88.88f;
    float timescale = 0.1f;
    light_dir = Vector3f(2.0f * sinf((float)elapsed_s * 1.5f * timescale),
        5.0f, 2.0f * cosf(2 + 1.9f * (float)elapsed_s * timescale));
    light_dir.normalize();
}


void drawScene(GLint program, Matrix4f V, Matrix4f P) {
    Matrix4f M = Matrix4f::identity();
    updateTransformUniforms(program, M, V, P);
    VertexRecorder recorder;
    for ( int i = 0; i < scene.batches.size(); i++ )
    {
        draw_batch batch = scene.batches[i];
        for (int j = batch.start_index; j < batch.start_index + batch.nindices; j++ )
        {
            uint32_t index = scene.indices[j];
            recorder.record(scene.positions[index], scene.normals[index], Vector3f(scene.texcoords[index], 0.f));
        }
        updateMaterialUniforms(program, batch.mat.diffuse, batch.mat.ambient, batch.mat.specular, batch.mat.shininess);
        
        GLuint gltexture = gltextures[batch.mat.diffuse_texture];
        glBindTexture(GL_TEXTURE_2D, gltexture);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, fb_depthtex);
        glActiveTexture(GL_TEXTURE0);
        
        recorder.draw();
    }
}

void draw() {
    // 2. DEPTH PASS
    // - bind framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // - configure viewport
    glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
    // - compute camera matrices (light source as camera)
    glUseProgram(program_color);
    // - call drawScene
    drawScene(program_color, getLightView(), getLightProjection());

    // 1. LIGHT PASS
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    int winw, winh;
    glfwGetFramebufferSize(window, &winw, &winh);
    glViewport(0, 0, winw, winh);
    glUseProgram(program_light);
    updateLightUniforms(program_light, light_dir, Vector3f(1.2f, 1.2f, 1.2f));
    // TODO IMPLEMENT drawScene
    drawScene(program_light, camera.GetViewMatrix(), camera.GetPerspective());

    // 3. DRAW DEPTH TEXTURE AS QUAD
    // drawTexturedQuad() helper in main.h is useful here.
    glViewport(0, 0, 256, 256);
    drawTexturedQuad(fb_depthtex);

    glViewport(256, 0, 256, 256);
    drawTexturedQuad(fb_colortex);
}

void loadTextures() {
    for (auto it = scene.textures.begin(); it != scene.textures.end(); ++it)
    {
        std::string name = it->first;
        rgbimage& im = it->second;
        // Create OpenGL Texture
        GLuint gltexture;
        glGenTextures(1, &gltexture);
        glBindTexture(GL_TEXTURE_2D, gltexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, im.w, im.h, 0, GL_RGB, GL_UNSIGNED_BYTE, im.data.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        gltextures.insert(std::make_pair(name, gltexture));
    }
}

void freeTextures() {
    for (auto it = gltextures.begin(); it != gltextures.end(); ++it)
    {
        std::string name = it->first;
        GLuint& gltexture = it->second;
        glDeleteTextures(1, &gltexture);
    }
}

void loadFramebuffer() {
    for (auto it = scene.textures.begin(); it != scene.textures.end(); ++it)
    {
        // color texture
        glGenTextures(1, &fb_colortex);
        glBindTexture(GL_TEXTURE_2D, fb_colortex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4096, 4096, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        // depth texture
        glGenTextures(1, &fb_depthtex);
        glBindTexture(GL_TEXTURE_2D, fb_depthtex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, 4096, 4096, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        glGenFramebuffers(1, &fb);
        glBindFramebuffer(GL_FRAMEBUFFER, fb); 
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, fb_colortex, 0); 
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_TEXTURE_2D, fb_depthtex, 0); 

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            printf("ERROR, incomplete framebuffer\n");
            exit(-1);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

void freeFramebuffer() {
    glDeleteTextures(1, &fb_colortex);
    glDeleteTextures(1, &fb_depthtex);
    glDeleteFramebuffers(1, &fb);
}

Matrix4f getLightView() {
    Vector3f center = Vector3f(0.f);
    Vector3f eye = 20*light_dir;
    Vector3f up = Vector3f::cross(-light_dir, Vector3f(1, 0, 0));
    return Matrix4f::lookAt(eye, center, up);
}

Matrix4f getLightProjection() {
    float xScale = 50;
    float yScale = 50;
    float zNear = 0;
    float zFar = 100;
    return Matrix4f::orthographicProjection(xScale, yScale, zNear, zFar);
}

// Main routine.
// Set up OpenGL, define the callbacks and start the main loop
int main(int argc, char* argv[])
{
    std::string basepath = "./";
    if (argc > 2) {
        printf("Usage: %s [basepath]\n", argv[0]);
    }
    else if (argc == 2) {
        basepath = argv[1];
    }
    printf("Loading scene and shaders relative to path %s\n", basepath.c_str());

    // load scene data
    // parsing code is in objparser.cpp
    // take a look at the public interface in objparser.h
    if (!scene.parse(basepath + "data/sponza_low/sponza_norm.obj")) {
        return -1;
    }

    window = createOpenGLWindow(1024, 1024, "Assignment 5");

    // setup the event handlers
    // key handlers are defined in main.h
    // take a look at main.h to know what's in there.
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseCallback);
    glfwSetCursorPosCallback(window, motionCallback);

    glClearColor(0.8f, 0.8f, 1.0f, 1);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // TODO add loadXYZ() function calls here
    loadTextures();
    loadFramebuffer();

    camera.SetDimensions(600, 600);
    camera.SetPerspective(50);
    camera.SetDistance(10);
    camera.SetCenter(Vector3f(0, 1, 0));
    camera.SetRotation(Matrix4f::rotateY(1.6f) * Matrix4f::rotateZ(0.4f));

    // set timer for animations
    int i = 0; // Need to store number of frames that have passed
    timer.set();
    while (!glfwWindowShouldClose(window)) {

        // Hack for macOS 10.14: at the second frame, resize the window very slightly
        // to trigger refresh
        // Fix submitted by Venkatesh Sivaraman
        if (i == 1) {
            int w, h;
            glfwGetWindowSize(window, &w, &h);
            glfwSetWindowSize(window, w - 1, h);
        }
        if (i <= 1) i++;

        setViewportWindow(window);

        // we reload the shader files each frame.
        // this shaders can be edited while the program is running
        // loadPrograms/freePrograms is implemented in main.h
        bool valid_shaders = loadPrograms(basepath);
        if (valid_shaders) {

            // draw coordinate axes
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (gMousePressed) {
                drawAxis();
            }

            // update animation
            updateLightDirection();

            // draw everything
            draw();
        }
        // make sure to release the shader programs.
        freePrograms();

        // Make back buffer visible
        glfwSwapBuffers(window);

        // Check if any input happened during the last frame
        glfwPollEvents();
    } // END OF MAIN LOOP

    // All OpenGL resource that are created with
    // glGen* or glCreate* must be freed.
    // TODO: add freeXYZ() function calls here
    freeFramebuffer();
    freeTextures();

    glfwDestroyWindow(window);


    return 0;	// This line is never reached.
}
