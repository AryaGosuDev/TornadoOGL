// TornadoOGL.cpp

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <iostream>
#include "Shaders.hpp"
#include "Texture.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_GTC_matrix_transform
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>

//#include <glm/glm.hpp>
//#include <glm/gtc/matrix_transform.hpp>
//#include <glm/gtx/norm.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

using namespace std;
GLFWwindow* window;

glm::vec3 mainEyeLoc(4.0, 6.0, 4.0);
glm::vec3 centerLoc(0.0, 3.0, 0.0);
glm::vec3 up(0.0, 1.0, 0.0);
float fov = glm::radians<float>(90.0f);
glm::mat4 projMatrix = glm::perspective(fov, (float)1024 / (float)768, 0.1f, 9.0f);
glm::mat4 viewMatrix = glm::lookAt(mainEyeLoc, centerLoc, up);
float TornadoHeight = 15.0f;
float TornadoInitialRadius = 1.0f;
float TornadoThinness = 6.0;
float TornadoHorizontalSpeed = 1.0;
float TornadoVerticalSpeed = 1.0;
int LastUsedParticle = 0;

double randMToN(double M, double N){
	return M + (rand() / (RAND_MAX / (N - M)));
}

// CPU representation of a particle
struct Particle {
	glm::vec3 pos, velocity, vortexCenter, vVelocity; float size, angle; unsigned char r, g, b, a; // Color
	float cameradistance; // *Squared* distance to the camera. if dead : -1.0f
	float height;

	Particle() {
		double randInitRadian = randMToN(0.0, 2 * glm::pi<double>());
		height = cameradistance = -1.0f;
		r = g = b = a = 255; pos = glm::vec3(cos ( randInitRadian), 0.0f, sin ( randInitRadian)); velocity = glm::normalize(glm::vec3(-1.0f, 0.0f, 0.0f));
		vortexCenter = glm::vec3(0.0f, 0.0f, 0.0f); vVelocity = glm::vec3(0.0f, 1.0f, 0.0f);
	}

	bool operator<(const Particle& that) const {
		// Sort in reverse order : far particles drawn first.
		return this->cameradistance > that.cameradistance;
	}
};

const int MaxParticles = 100'000;
Particle ParticlesContainer[MaxParticles];

void SortParticles() {
	std::sort(&ParticlesContainer[0], &ParticlesContainer[MaxParticles]);
}

// Finds a Particle in ParticlesContainer which isn't used yet.
// (i.e. life < 0);
int FindUnusedParticle() {

	for (int i = LastUsedParticle; i < MaxParticles; i++) {
		if (ParticlesContainer[i].height < 0.0f) {
			LastUsedParticle = i;
			return i;
		}
	}
	if (LastUsedParticle >= MaxParticles && ParticlesContainer[0].height < -0.5f) {
		LastUsedParticle = 0;
		return LastUsedParticle;
	}
	return -1;
}

//OGL SUX -> USE VULKAN
int main() {

	if (!glfwInit()){
		fprintf(stderr, "Failed to initialize GLFW\n");getchar();return -1;
	}

	glfwWindowHint(GLFW_SAMPLES, 4);
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // To make MacOS happy; should not be needed
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// Open a window and create its OpenGL context
	window = glfwCreateWindow(1024, 768, "Tornado (OpenGL Sux, use Vulkan)", NULL, NULL);
	if (window == NULL) {
		fprintf(stderr, "Failed to open GLFW window.\n");
		getchar();
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

	// Initialize GLEW
	glewExperimental = true; // Needed for core profile
	if (glewInit() != GLEW_OK) {
		fprintf(stderr, "Failed to initialize GLEW\n");
		getchar();
		glfwTerminate();
		return -1;
	}

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	ImGui::StyleColorsClassic();
	// Setup Platform/Renderer bindings
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 330");
	bool show_demo_window = true;
	bool show_another_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	// Ensure we can capture the escape key being pressed below
	glfwSetInputMode(window, GLFW_STICKY_KEYS, GL_TRUE);
	// Hide the mouse and enable unlimited mouvement
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

	// Set the mouse at the center of the screen
	glfwPollEvents();
	glfwSetCursorPos(window, 1024 / 2, 768 / 2);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	// Enable depth test
	glEnable(GL_DEPTH_TEST);
	// Accept fragment if it closer to the camera than the former one
	glDepthFunc(GL_LESS);

	GLuint VertexArrayID;
	glGenVertexArrays(1, &VertexArrayID);
	glBindVertexArray(VertexArrayID);

	// Create and compile our GLSL program from the shaders
	GLuint programID = LoadShaders("Particle.vertexshader", "Particle.fragmentshader");

	// Vertex shader
	GLuint CameraRight_worldspace_ID = glGetUniformLocation(programID, "CameraRight_worldspace");
	GLuint CameraUp_worldspace_ID = glGetUniformLocation(programID, "CameraUp_worldspace");
	GLuint ViewProjMatrixID = glGetUniformLocation(programID, "VP");
	// fragment shader
	GLuint TextureID = glGetUniformLocation(programID, "myTextureSampler");

	static GLfloat* g_particule_position_size_data = new GLfloat[MaxParticles * 4];
	static GLubyte* g_particule_color_data = new GLubyte[MaxParticles * 4];

	//array init
	for (int i = 0; i < MaxParticles; i++) {
		ParticlesContainer[i].cameradistance = -1.0f;
		ParticlesContainer[i].height = -1.0f;
		double randInitRadian = randMToN(0.0, 2 * glm::pi<double>());
		ParticlesContainer[i].pos = glm::vec3(cos(randInitRadian), 0.0f, sin(randInitRadian));
		ParticlesContainer[i].vortexCenter = glm::vec3(0.0f, 0.0f, 0.0f);
		ParticlesContainer[i].velocity = glm::normalize(ParticlesContainer[i].velocity);
		ParticlesContainer[i].vVelocity.y = TornadoVerticalSpeed * ParticlesContainer[i].vVelocity.y;
	}
	GLuint Texture = loadDDS("particle.DDS");

	// The VBO containing the 4 vertices of the particles.
	// Thanks to instancing, they will be shared by all particles.
	static const GLfloat g_vertex_buffer_data[] = {
		 -0.5f, -0.5f, 0.0f,
		  0.5f, -0.5f, 0.0f,
		 -0.5f,  0.5f, 0.0f,
		  0.5f,  0.5f, 0.0f,
	};
	GLuint billboard_vertex_buffer;
	glGenBuffers(1, &billboard_vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, billboard_vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(g_vertex_buffer_data), g_vertex_buffer_data, GL_STATIC_DRAW);

	// The VBO containing the positions and sizes of the particles
	GLuint particles_position_buffer;
	glGenBuffers(1, &particles_position_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, particles_position_buffer);
	// Initialize with empty (NULL) buffer : it will be updated later, each frame.
	glBufferData(GL_ARRAY_BUFFER, MaxParticles * 4 * sizeof(GLfloat), NULL, GL_STREAM_DRAW);

	// The VBO containing the colors of the particles
	GLuint particles_color_buffer;
	glGenBuffers(1, &particles_color_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, particles_color_buffer);
	// Initialize with empty (NULL) buffer : it will be updated later, each frame.
	glBufferData(GL_ARRAY_BUFFER, MaxParticles * 4 * sizeof(GLubyte), NULL, GL_STREAM_DRAW);

	glm::vec3 CameraPosition(glm::inverse(viewMatrix)[3]);
	glm::mat4 ViewProjectionMatrix = projMatrix * viewMatrix;

	double lastTime = glfwGetTime();
	do {
		glfwPollEvents();
		// Clear the screen
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		double currentTime = glfwGetTime();
		double delta = currentTime - lastTime;
		lastTime = currentTime;

		// Generate a fixed number of particles to account for the variable speed rate of particles
		// which is related to their invalid state.
		// A particle is marked invalid once it reaches the full height of the tornado.
		int newparticles = 10;

		for (int i = 0; i < newparticles; i++) {
			int particleIndex = FindUnusedParticle();
			if (particleIndex != -1) {
				ParticlesContainer[particleIndex].height = 0.0f;
				double randInitRadian = randMToN(0.0, 2 * glm::pi<double>());
				ParticlesContainer[particleIndex].pos = glm::vec3(cos(randInitRadian), 0.0f, sin(randInitRadian));
				ParticlesContainer[particleIndex].vortexCenter = glm::vec3(0.0f, 0.0f, 0.0f);
				// Very bad way to generate a random color
				ParticlesContainer[particleIndex].r = rand() % 256;
				ParticlesContainer[particleIndex].g = rand() % 256;
				ParticlesContainer[particleIndex].b = rand() % 256;

				ParticlesContainer[particleIndex].size = 0.05f;
			}
		}

		// Simulate all particles
		int ParticlesCount = 0;
		for (int i = 0; i < MaxParticles; i++) {

			Particle& p = ParticlesContainer[i]; // shortcut

			if (p.height >= 0.0f && p.height <= TornadoHeight) {

				// Simulate simple physics : gravity only, no collisions
				p.cameradistance = glm::dot(p.pos - CameraPosition, p.pos - CameraPosition);

				p.vortexCenter.y = p.pos.y;
				p.pos.y -= p.pos.y;
				glm::vec3 interPosHorizontal = (float)delta * p.velocity + p.pos;
				glm::vec3 centerDir = glm::normalize(interPosHorizontal);
				//projection from center to a point on an expanding circle
				centerDir = centerDir * (TornadoInitialRadius + (TornadoInitialRadius * (p.vortexCenter.y / TornadoThinness)));
				p.pos.x = centerDir.x; p.pos.z = centerDir.z;
				p.velocity = TornadoHorizontalSpeed * glm::normalize(glm::cross(p.pos, up));
				p.vVelocity = TornadoVerticalSpeed * glm::normalize(p.vVelocity);
				p.pos.y = p.height = (float)delta * p.vVelocity.y + p.vortexCenter.y;

				// Fill the GPU buffer
				g_particule_position_size_data[4 * ParticlesCount + 0] = p.pos.x;
				g_particule_position_size_data[4 * ParticlesCount + 1] = p.pos.y;
				g_particule_position_size_data[4 * ParticlesCount + 2] = p.pos.z;
				g_particule_position_size_data[4 * ParticlesCount + 3] = p.size;

				g_particule_color_data[4 * ParticlesCount + 0] = p.r;
				g_particule_color_data[4 * ParticlesCount + 1] = p.g;
				g_particule_color_data[4 * ParticlesCount + 2] = p.b;
				g_particule_color_data[4 * ParticlesCount + 3] = p.a;
			}
			else {
				p.cameradistance = -1.0f;
				p.pos.y = p.height = -1.0f;
				p.size = 0.0f;
			}
			if (p.height >= 0.0f && p.height <= TornadoHeight) ParticlesCount++;
			
		}

		SortParticles();
		
		// PARTICLE MANIPULATION CODE
		glBindBuffer(GL_ARRAY_BUFFER, particles_position_buffer);
		glBufferData(GL_ARRAY_BUFFER, MaxParticles * 4 * sizeof(GLfloat), NULL, GL_STREAM_DRAW); // Buffer orphaning, a common way to improve streaming perf. See above link for details.
		glBufferSubData(GL_ARRAY_BUFFER, 0, ParticlesCount * sizeof(GLfloat) * 4, g_particule_position_size_data);
		
		glBindBuffer(GL_ARRAY_BUFFER, particles_color_buffer);
		glBufferData(GL_ARRAY_BUFFER, MaxParticles * 4 * sizeof(GLubyte), NULL, GL_STREAM_DRAW); // Buffer orphaning, a common way to improve streaming perf. See above link for details.
		glBufferSubData(GL_ARRAY_BUFFER, 0, ParticlesCount * sizeof(GLubyte) * 4, g_particule_color_data);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// Use our shader
		glUseProgram(programID);

		// Bind our texture in Texture Unit 0
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, Texture);
		// Set our "myTextureSampler" sampler to use Texture Unit 0
		glUniform1i(TextureID, 0);

		// Same as the billboards tutorial
		glUniform3f(CameraRight_worldspace_ID, viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
		glUniform3f(CameraUp_worldspace_ID, viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);

		glUniformMatrix4fv(ViewProjMatrixID, 1, GL_FALSE, &ViewProjectionMatrix[0][0]);

		// 1rst attribute buffer : vertices
		glEnableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, billboard_vertex_buffer);
		glVertexAttribPointer(
			0,                  // attribute. No particular reason for 0, but must match the layout in the shader.
			3,                  // size
			GL_FLOAT,           // type
			GL_FALSE,           // normalized?
			0,                  // stride
			(void*)0            // array buffer offset
		);

		// 2nd attribute buffer : positions of particles' centers
		glEnableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER, particles_position_buffer);
		glVertexAttribPointer(
			1,                                // attribute. No particular reason for 1, but must match the layout in the shader.
			4,                                // size : x + y + z + size => 4
			GL_FLOAT,                         // type
			GL_FALSE,                         // normalized?
			0,                                // stride
			(void*)0                          // array buffer offset
		);

		// 3rd attribute buffer : particles' colors
		glEnableVertexAttribArray(2);
		glBindBuffer(GL_ARRAY_BUFFER, particles_color_buffer);
		glVertexAttribPointer(
			2,                                // attribute. No particular reason for 1, but must match the layout in the shader.
			4,                                // size : r + g + b + a => 4
			GL_UNSIGNED_BYTE,                 // type
			GL_TRUE,                          // normalized?    *** YES, this means that the unsigned char[4] will be accessible with a vec4 (floats) in the shader ***
			0,                                // stride
			(void*)0                          // array buffer offset
		);

		// These functions are specific to glDrawArrays*Instanced*.
		// The first parameter is the attribute buffer we're talking about.
		// The second parameter is the "rate at which generic vertex attributes advance when rendering multiple instances"
		// http://www.opengl.org/sdk/docs/man/xhtml/glVertexAttribDivisor.xml
		glVertexAttribDivisor(0, 0); // particles vertices : always reuse the same 4 vertices -> 0
		glVertexAttribDivisor(1, 1); // positions : one per quad (its center)                 -> 1
		glVertexAttribDivisor(2, 1); // color : one per quad    

		// Draw the particules !
		// This draws many times a small triangle_strip (which looks like a quad).
		// This is equivalent to :
		// for(i in ParticlesCount) : glDrawArrays(GL_TRIANGLE_STRIP, 0, 4), 
		// but faster.
		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, ParticlesCount);
		//glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, MaxParticles);

		glDisableVertexAttribArray(0);
		glDisableVertexAttribArray(1);
		glDisableVertexAttribArray(2);

		//IM gui draw
		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		//if (show_demo_window) ImGui::ShowDemoWindow(&show_demo_window);

		// 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
		{
			static float f = 0.0f;
			static int counter = 0;

			ImGui::Begin("Tornado!");                          // Create a window called "Hello, world!" and append into it.

			ImGui::Text("Change Tornado Settings:");               // Display some text (you can use a format strings too)
			//ImGui::Checkbox("", &show_demo_window);      // Edit bools storing our window open/close state
			//ImGui::Checkbox("Vertical Speed.", &show_another_window);

			ImGui::SliderFloat("Horizontal Speed.", &TornadoHorizontalSpeed, 0.3f, 3.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
			ImGui::SliderFloat("Vertical Speed.", &TornadoVerticalSpeed, 0.3f, 2.0f);
			ImGui::SliderFloat("Thinness.", &TornadoThinness, 1.0f, 8.0f);
			//ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

			//if (ImGui::Button("Button")) counter++;             // Buttons return true when clicked (most widgets return true when edited/activated)
				
			//ImGui::SameLine();
			//ImGui::Text("counter = %d", counter);

			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
			ImGui::End();
		}
		/*
		// 3. Show another simple window.
		if (show_another_window)
		{
			ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
			ImGui::Text("Hello from another window!");
			if (ImGui::Button("Close Me"))
				show_another_window = false;
			ImGui::End();
		}
		*/
		// Rendering
		ImGui::Render();
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		//glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
		//glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// Swap buffers
		glfwSwapBuffers(window);

		
	} // Check if the ESC key was pressed or the window was closed
	while (glfwGetKey(window, GLFW_KEY_ESCAPE) != GLFW_PRESS && glfwWindowShouldClose(window) == 0);
    
	delete[] g_particule_position_size_data;

	// Cleanup ImGui
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	// Cleanup VBO and shader
	glDeleteBuffers(1, &particles_color_buffer);
	glDeleteBuffers(1, &particles_position_buffer);
	glDeleteBuffers(1, &billboard_vertex_buffer);
	glDeleteProgram(programID);
	glDeleteTextures(1, &Texture);
	glDeleteVertexArrays(1, &VertexArrayID);
	// Close OpenGL window and terminate GLFW
	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}
