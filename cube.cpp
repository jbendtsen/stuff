#include "glad/glad.h"
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <cstring>
#include <math.h>

static const char* vertex_shader_text =
"#version 420 core\n"
"uniform mat4 MVP;\n"
"uniform float time;\n"
"uniform vec3 light;\n"
"attribute vec3 pos;\n"
"attribute vec3 normal;\n"
"out vec3 vertColor;\n"
"vec3 getHue() {\n"
"    float a = (1 + sin(time)) / 2;\n"
"    float b = (1 + cos(time / 1.618034)) / 2;\n"
"    float c = (1 + sin(time / (3.141593 / 3))) / 2;\n"
"    return vec3(a, b, c);\n"
"}\n"
"float getLuma() {\n"
"    vec3 n = normalize(normal);\n"
"    vec3 l = normalize(light);\n"
"    return clamp(dot(n, l), 0, 1);\n"
"}\n"
"void main() {\n"
"    gl_Position = MVP * vec4(pos, 1.0);\n"
"    vec3 hue = getHue() + vec3(0.2, 0.2, 0.2);\n"
"    vertColor = hue * 0.8f * getLuma();\n"
"}\n";

static const char* fragment_shader_text =
"#version 420 core\n"
"in vec3 vertColor;\n"
"out vec3 color;\n"
"void main() {\n"
"    color = vertColor;\n"
"}\n";

static const GLfloat cube_mesh[] = {
	-1.0f,-1.0f,-1.0f, /**/ -1.0f,-1.0f, 1.0f, /**/ -1.0f, 1.0f, 1.0f,
	 1.0f, 1.0f,-1.0f, /**/ -1.0f,-1.0f,-1.0f, /**/ -1.0f, 1.0f,-1.0f,
	 1.0f,-1.0f, 1.0f, /**/ -1.0f,-1.0f,-1.0f, /**/  1.0f,-1.0f,-1.0f,
	 1.0f, 1.0f,-1.0f, /**/  1.0f,-1.0f,-1.0f, /**/ -1.0f,-1.0f,-1.0f,
	-1.0f,-1.0f,-1.0f, /**/ -1.0f, 1.0f, 1.0f, /**/ -1.0f, 1.0f,-1.0f,
	 1.0f,-1.0f, 1.0f, /**/ -1.0f,-1.0f, 1.0f, /**/ -1.0f,-1.0f,-1.0f,
	-1.0f, 1.0f, 1.0f, /**/ -1.0f,-1.0f, 1.0f, /**/  1.0f,-1.0f, 1.0f,
	 1.0f, 1.0f, 1.0f, /**/  1.0f,-1.0f,-1.0f, /**/  1.0f, 1.0f,-1.0f,
	 1.0f,-1.0f,-1.0f, /**/  1.0f, 1.0f, 1.0f, /**/  1.0f,-1.0f, 1.0f,
	 1.0f, 1.0f, 1.0f, /**/  1.0f, 1.0f,-1.0f, /**/ -1.0f, 1.0f,-1.0f,
	 1.0f, 1.0f, 1.0f, /**/ -1.0f, 1.0f,-1.0f, /**/ -1.0f, 1.0f, 1.0f,
	 1.0f, 1.0f, 1.0f, /**/ -1.0f, 1.0f, 1.0f, /**/  1.0f,-1.0f, 1.0f
};

static GLfloat cube_normals[108] = {0};

typedef struct {
	float max_speed;
	float max_acc;
	float disp = 0;
	float vel = 0;
	float acc = 0;
} Control;

Control lng, lat, zoom;

void keys_handler(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GLFW_TRUE);

	if (action == GLFW_PRESS) {
		switch (key) {
			case GLFW_KEY_APOSTROPHE:
				zoom.acc = -zoom.max_acc;
				break;
			case GLFW_KEY_SLASH:
				zoom.acc = zoom.max_acc;
				break;
			case GLFW_KEY_UP:
				lat.acc = -lat.max_acc;
				break;
			case GLFW_KEY_DOWN:
				lat.acc = lat.max_acc;
				break;
			case GLFW_KEY_LEFT:
				lng.acc = -lng.max_acc;
				break;
			case GLFW_KEY_RIGHT:
				lng.acc = lng.max_acc;
				break;
		}
	}
	else if (action == GLFW_RELEASE) {
		switch (key) {
			case GLFW_KEY_APOSTROPHE:
			case GLFW_KEY_SLASH:
				zoom.acc = 0.0f;
				break;
			case GLFW_KEY_UP:
			case GLFW_KEY_DOWN:
				lat.acc = 0.0f;
				break;
			case GLFW_KEY_LEFT:
			case GLFW_KEY_RIGHT:
				lng.acc = 0.0f;
				break;
		}
	}
}

void tick_control(Control& c) {
	float vel = c.vel + c.acc;
	if (vel >= -c.max_speed && vel <= c.max_speed)
		c.vel = vel;

	if (c.acc > -0.0001f && c.acc < 0.0001f) {
		float friction = c.max_acc * 0.2f;
		if (c.vel > 0.001f)
			c.vel -= friction;
		else if (c.vel < -0.001f)
			c.vel += friction;
		else
			c.vel = 0;
	}

	c.disp += vel;
}

void tick_camera(glm::mat4& view) {
	tick_control(lng);
	tick_control(lat);
	tick_control(zoom);

	float *v = glm::value_ptr(view);
	v[15] = 1.0f;
	v[14] = -zoom.disp;

	glm::vec3 right(v[0], v[1], v[2]);
	glm::vec3 up(v[4], v[5], v[6]);
	glm::vec3 fw(v[8], v[9], v[10]);

	fw = glm::normalize(fw + lat.vel * up);
	up = glm::cross(fw, right);
	fw = glm::cross(up, right);

	fw = glm::normalize(fw + lng.vel * right);
	right = glm::cross(fw, up);
	fw = glm::cross(up, -right);

	v[0] = right.x;
	v[1] = right.y;
	v[2] = right.z;
	v[4] = up.x;
	v[5] = up.y;
	v[6] = up.z;
	v[8] = fw.x;
	v[9] = fw.y;
	v[10] = fw.z;

	view = glm::make_mat4x4(&v[0]);
}

int main() {
	lng.max_speed = 0.1f;
	lng.max_acc = 0.01f;
	lat.max_speed = 0.1f;
	lat.max_acc = 0.01f;
	zoom.max_speed = 0.2f;
	zoom.max_acc = 0.02f;
	zoom.disp = 7.0f;

	for (int i = 0; i < 12; i++) {
		GLfloat *m = (GLfloat*)&cube_mesh[i * 9];

		glm::vec3 v1(m[0], m[1], m[2]);
		glm::vec3 a = glm::vec3(m[3], m[4], m[5]) - v1;
		glm::vec3 b = glm::vec3(m[6], m[7], m[8]) - v1;
		glm::vec3 normal = glm::normalize(glm::cross(a, b));

		GLfloat *norms = &cube_normals[i * 9];
		memcpy(norms, &normal[0], 3 * sizeof(float));
		memcpy(norms + 3, &normal[0], 3 * sizeof(float));
		memcpy(norms + 6, &normal[0], 3 * sizeof(float));
	}

	GLFWwindow* window;

	if (!glfwInit())
		return 1;

	glfwWindowHint(GLFW_SAMPLES, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	window = glfwCreateWindow(640, 480, "Hello World", NULL, NULL);
	if (!window) {
		glfwTerminate();
		return 2;
	}

	glfwSetKeyCallback(window, keys_handler);

	glfwMakeContextCurrent(window);

	if (!gladLoadGL()) {
		glfwDestroyWindow(window);
		glfwTerminate();
		return 3;
	}

	// Enable v-sync by specifying that a buffer swap happens after 1 screen update
	glfwSwapInterval(1);

	glfwSetInputMode(window, GLFW_STICKY_KEYS, GL_TRUE);

	glClearColor(0.15f, 0.0f, 0.3f, 0.0f);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	GLuint VertexArrayID;
	glGenVertexArrays(2, &VertexArrayID);
	glBindVertexArray(VertexArrayID);

	GLuint cube_mesh_id;
	glGenBuffers(1, &cube_mesh_id);
	glBindBuffer(GL_ARRAY_BUFFER, cube_mesh_id);
	glBufferData(GL_ARRAY_BUFFER, sizeof(cube_mesh), &cube_mesh[0], GL_STATIC_DRAW);

	GLuint cube_normals_id;
	glGenBuffers(1, &cube_normals_id);
	glBindBuffer(GL_ARRAY_BUFFER, cube_normals_id);
	glBufferData(GL_ARRAY_BUFFER, sizeof(cube_normals), &cube_normals[0], GL_STATIC_DRAW);

	GLuint frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(frag_shader, 1, &fragment_shader_text, NULL);
	glCompileShader(frag_shader);

	GLuint vert_shader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vert_shader, 1, &vertex_shader_text, NULL);
	glCompileShader(vert_shader);

	GLuint program = glCreateProgram();
	glAttachShader(program, frag_shader);
	glAttachShader(program, vert_shader);
	glLinkProgram(program);

	GLint mvp_loc = glGetUniformLocation(program, "MVP");
	GLint time_loc = glGetUniformLocation(program, "time");
	GLint light_loc = glGetUniformLocation(program, "light");
	GLint pos_loc = glGetAttribLocation(program, "pos");
	GLint normal_loc = glGetAttribLocation(program, "normal");

	glm::mat4 view(1.0f);

	while (!glfwWindowShouldClose(window)) {
		int width, height;
		glfwGetFramebufferSize(window, &width, &height);
		float ratio = (float)width / (float)height;

		glViewport(0, 0, width, height);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glm::mat4 projection = glm::perspective(glm::radians(45.0f), ratio, 0.1f, 100.0f);
		tick_camera(view);
		glm::mat4 mvp = projection * view;

		glUseProgram(program);

		glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, (const GLfloat*)&mvp[0]);
		glUniform1f(time_loc, glfwGetTime());

		GLfloat *v = (GLfloat*)glm::value_ptr(view);
		glUniform3f(light_loc, v[2], v[6], v[10]);

		glEnableVertexAttribArray(pos_loc);
		glBindBuffer(GL_ARRAY_BUFFER, cube_mesh_id);
		glVertexAttribPointer(pos_loc, 3, GL_FLOAT, 0, 0, NULL);

		glEnableVertexAttribArray(normal_loc);
		glBindBuffer(GL_ARRAY_BUFFER, cube_normals_id);
		glVertexAttribPointer(normal_loc, 3, GL_FLOAT, 0, 0, NULL);

		glDrawArrays(GL_TRIANGLES, 0, 12*3);
		glDisableVertexAttribArray(pos_loc);

		/* Swap front and back buffers */
		glfwSwapBuffers(window);
		/* Poll for and process events */
		glfwPollEvents();
	}

	glDeleteBuffers(1, &cube_mesh_id);
	glDeleteBuffers(1, &cube_normals_id);
	glDeleteProgram(program);
	glDeleteVertexArrays(1, &VertexArrayID);

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
