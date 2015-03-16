#include <stdio.h>
#include <GL/glew.h>
#include <GL/glut.h>
#include <math.h>
#include <pthread.h>

typedef struct Point {
	double i, j;
} Point;
double Distance(Point *p1, Point *p2) {
	return sqrt((p1->i-p2->i)*(p1->i-p2->i) + (p1->j-p2->j)*(p1->j-p2->j));
}
int PointEquals(Point *p1, Point *p2) {
	return p1->i == p2->i && p1->j == p2->j;
}
int GuessDiverged(Point *p) {
	if (p->i < -2 || p->i > 2 || p->j < -2 || p-> j > 2) return 1;
	return 0;
}
int Diverged(Point *p) {
	return sqrt(p->i*p->i + p->j*p->j) >= 2;
}
void Iterate(Point *c, Point *z) {
	double a = z->i;
	z->i = a*a - z->j*z->j + c->i;
	z->j = 2*a*z->j + c->j;
}

typedef struct PointData {
	Point point; // Center point
	int iter; // Iterations (+undiverged, -diverged, 0 unknown)
} PointData;

typedef struct FracData {
	Point center;
	double zoom; // negative log to base 2 of sqrt of area
	int height, width;
	PointData *data;
} FracData;

int maxiter = 128;

FracData fd1, fd2;
// The width and height are the window width and height in pixels
FracData focus;
FracData *current = NULL;

void ExitWithError() {
	if (fd1.data) free(fd1.data);
	if (fd2.data) free(fd2.data);
	fprintf(stderr,"An error occurred.\n");
	perror(NULL);
	exit(1);
}

void Populate(FracData *fd) {
	fd->data = realloc(fd->data,fd->width*fd->height*sizeof(PointData));
	if (!fd->data) ExitWithError(1);
	double sqrta = sqrt((double)fd->height/fd->width);
	double dx = pow(2.0,-fd->zoom) * sqrta / fd->height;
	// Center of lower left corner pixel
	PointData p1;
	p1.point.i = fd->center.i + dx/2*(1-fd->width);
	p1.point.j = fd->center.j + dx/2*(1-fd->height);
	// Pixel we are currently iterating
	PointData p2 = p1;
	int i = 0;
	for (int y = 0; y < fd->height; y++) {
		for (int x = 0; x < fd->width; x++) {
			PointData p3 = { .point = { .i = 0, .j = 0 }, .iter = 0 };
			int iter;
			for (iter = 0; iter < maxiter; iter++) {
				Iterate(&p2.point,&p3.point);
				if (Diverged(&p3.point)) {
					p3.iter = -iter - 1;
					break;
				}
			}
			if (iter == maxiter) {
				p3.iter = iter;
			}
			fd->data[i++] = p3;
			p2.point.i += dx;
		}
		p2.point.i = p1.point.i;
		p2.point.j += dx;
	}
}

int LoadShaders(const char * vertpath, const char * fragpath) {
	// Open both files
	FILE *vertf = NULL, *fragf = NULL;
	vertf = fopen(vertpath,"r");
	if (!vertf) {
		return -1;
	}
	fragf = fopen(fragpath,"r");
	if (!fragf) {
		fclose(vertf);
		return -1;
	}

	GLchar *vertdat = NULL, *fragdat = NULL;
	GLint vertcnt = 0, fragcnt = 0;
	size_t vertsize = 0, fragsize = 0;

#define ReadLen 256
	// Read vertex shader source
	while (!feof(vertf)) {
		if (vertsize < vertcnt + ReadLen) {
			vertsize += ReadLen;
			vertdat = realloc(vertdat,vertsize);
			if (!vertdat) {
				fclose(vertf); fclose(fragf);
				free(vertdat);
				return -1;
			}
		}
		vertcnt += fread(vertdat+vertcnt,1,ReadLen,vertf);
		if (ferror(vertf)) {
			fclose(vertf); fclose(fragf);
			free(vertdat);
			return -1;
		}
	}
	fclose(vertf);

	// Read fragment shader source
	while (!feof(fragf)) {
		if (fragsize < fragcnt + ReadLen) {
			fragsize += ReadLen;
			fragdat = realloc(fragdat,fragsize);
			if (!fragdat) {
				fclose(vertf); fclose(fragf);
				free(fragdat); free(vertdat);
				return -1;
			}
		}
		fragcnt += fread(fragdat+fragcnt,1,ReadLen,fragf);
		if (ferror(fragf)) {
			fclose(vertf); fclose(fragf);
			free(fragdat); free(vertdat);
			return -1;
		}
	}
	fclose(fragf);

	// Create the shaders
	GLuint vertid = glCreateShader(GL_VERTEX_SHADER);
	GLuint fragid = glCreateShader(GL_FRAGMENT_SHADER);

	GLint result = GL_FALSE;

	// Compile vertex shader
	printf("Compiling vertex shader : %s\n", vertpath);
	glShaderSource(vertid, 1, (const GLchar**)&vertdat, &vertcnt);
	free(vertdat);
	glCompileShader(vertid);

	// Check vertex shader
	glGetShaderiv(vertid, GL_COMPILE_STATUS, &result);
	if (result != GL_TRUE) {
		GLint infolen;
		glGetShaderiv(vertid, GL_INFO_LOG_LENGTH, &infolen);
		GLchar *message = malloc(infolen+1);
		if (!message) goto vertcomperr;
		GLsizei len;
		glGetShaderInfoLog(vertid, infolen, &len, message);
		message[len] = 0;
		fprintf(stderr, "Error compiling vertex shader %s: %s\n", vertpath, message);
		free(message);
		vertcomperr:
		free(fragdat);
		return -1;
	}

	// Compile fragment shader
	printf("Compiling fragment shader : %s\n", fragpath);
	glShaderSource(fragid, 1, (const GLchar**)&fragdat, &fragcnt);
	free(fragdat);
	glCompileShader(fragid);

	// Check Fragment Shader
	glGetShaderiv(vertid, GL_COMPILE_STATUS, &result);
	if (result != GL_TRUE) {
		GLint infolen;
		glGetShaderiv(fragid, GL_INFO_LOG_LENGTH, &infolen);
		GLchar *message = malloc(infolen+1);
		if (!message) return -1;
		GLsizei len;
		glGetShaderInfoLog(fragid, infolen, &len, message);
		message[len] = 0;
		fprintf(stderr, "Error compiling fragment shader %s: %s\n", fragpath, message);
		free(message);
		return -1;
	}

	// Link the program
	printf("Linking program\n");
	GLuint progid = glCreateProgram();
	glAttachShader(progid, vertid);
	glAttachShader(progid, fragid);
	glLinkProgram(progid);

	// Check the program
	glGetProgramiv(progid, GL_LINK_STATUS, &result);
	if (result != GL_TRUE) {
		GLint infolen;
		glGetProgramiv(progid, GL_INFO_LOG_LENGTH, &infolen);
		GLchar *message = malloc(infolen+1);
		if (!message) return -1;
		GLsizei len;
		glGetProgramInfoLog(progid, infolen, &len, message);
		message[len] = 0;
		printf("Error linking program: %s\n", message);
		free(message);
		return -1;
	}

	glDeleteShader(vertid);
	glDeleteShader(fragid);

	return progid;
}

void fractalReshape(int width, int height) {
	focus.height = height, focus.width = width;
	glViewport(0,0,width,height);
}

int FracDataApplies(FracData *fd, FracData *focus) {
	return PointEquals(&fd->center,&focus->center) &&
		(fd->height == focus->height) &&
		(fd->width == focus->width) &&
		(fd->zoom == floor(focus->zoom));
}

void GenFractalTexture(PointData *pd, int len, char *tbuf) {
	for (int i = 0; i < len; i++) {
		// White - diverged
		if (pd[i].iter < 0) {
			tbuf[i*3] = tbuf[i*3+1] = tbuf[i*3+2] = 0xff;
		}
		// Black - undiverged
		else {
			tbuf[i*3] = tbuf[i*3+1] = tbuf[i*3+2] = 0x00;
		}
	}
}

GLuint tex = 0;
char *tbuf = NULL;
int tbufsize = 0;
void GenerateFractalTexture(void) {
	/*
	if (tex) {
		glDeleteTextures(1,&tex);
	}
	*/
	//glEnable(GL_TEXTURE_2D);
	tbuf = realloc(tbuf,current->width*current->height*3);
	if (!tbuf) ExitWithError(1);
	tbufsize = current->width*current->height*3;
	GenFractalTexture(current->data,current->width*current->height,tbuf);
	if (tex == 0) {
		glGenTextures(1,&tex);
		glBindTexture(GL_TEXTURE_2D,tex);
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,current->width,current->height,0,GL_RGB,GL_UNSIGNED_BYTE,tbuf);
	} else {
		glBindTexture(GL_TEXTURE_2D,tex);
		//glTexSubImage2D(GL_TEXTURE_2D,0,0,0,current->width,current->height,GL_RGB,GL_UNSIGNED_BYTE,tbuf);
	}
	glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
	float color[] = { 1.0f, 0.0f, 0.0f, 1.0f };
	glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,color);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D,tex);
}

pthread_mutex_t dispMutex = PTHREAD_MUTEX_INITIALIZER;
void fractalDisplay(void) {
	int ret = pthread_mutex_lock(&dispMutex);
	if (ret) ExitWithError(1);
	focus.width = glutGet(GLUT_WINDOW_WIDTH);
	focus.height = glutGet(GLUT_WINDOW_HEIGHT);
	if (current) {
		if (FracDataApplies(&fd1,&focus)) current = &fd1;
		else if (FracDataApplies(&fd2,&focus)) current = &fd2;
		else { // Fix the one farthest from focus
			if (abs(focus.zoom-fd1.zoom) > abs(focus.zoom-fd2.zoom)) {
				fd1.zoom = floor(focus.zoom);
				current = &fd1;
			} else {
				fd2.zoom = floor(focus.zoom);
				current = &fd2;
			}
			current->center = focus.center;
			current->height = focus.height;
			current->width = focus.width;
			if (current->width > GL_MAX_TEXTURE_SIZE) current->width = GL_MAX_TEXTURE_SIZE;
			if (current->height > GL_MAX_TEXTURE_SIZE) current->height = GL_MAX_TEXTURE_SIZE;
			Populate(current);
		}
	} else { // FracData's need to be populated
		fd1.zoom = floor(focus.zoom);
		fd2.zoom = fd1.zoom - 1;
		fd1.height = fd2.height = focus.height;
		fd1.width = fd2.width = focus.width;
		if (fd1.width > GL_MAX_TEXTURE_SIZE) fd1.width = GL_MAX_TEXTURE_SIZE;
		if (fd1.height > GL_MAX_TEXTURE_SIZE) fd1.height = GL_MAX_TEXTURE_SIZE;
		if (fd2.width > GL_MAX_TEXTURE_SIZE) fd2.width = GL_MAX_TEXTURE_SIZE;
		if (fd2.height > GL_MAX_TEXTURE_SIZE) fd2.height = GL_MAX_TEXTURE_SIZE;
		Populate(&fd1);
		Populate(&fd2);
		current = &fd1;
	}
	
	GenerateFractalTexture();
	//glEnable(GL_TEXTURE_2D);
	//glBindTexture(GL_TEXTURE_2D,tex);
	glClearColor(0.0,0.0,0.4,1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glBegin(GL_QUADS);
		glTexCoord2f(0,0);
		glVertex2f(-1.0,-1.0);
		glTexCoord2f(1,0);
		glVertex2f(1.0,-1.0);
		glTexCoord2f(1,1);
		glVertex2f(1.0,1.0);
		glTexCoord2f(0,1);
		glVertex2f(-1.0,1.0);
	glEnd();
	//glDisable(GL_TEXTURE_2D);
	glutSwapBuffers();
	ret = pthread_mutex_unlock(&dispMutex);
	if (ret) ExitWithError(1);
}

int main(int argc, char **argv) {
	int width = 400, height = 400;

	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
	glutInitWindowSize(width, height);
	glutInitWindowPosition(100, 100);
	glutCreateWindow("BrotBot");
	glutReshapeFunc(fractalReshape);
	glutDisplayFunc(fractalDisplay);
	
	GLenum err = glewInit();
	if (err != GLEW_OK) {
		fprintf(stderr, "GLEW init failed: %s\n", glewGetErrorString(err));
		return 1;
	}

	/*
	unsigned char *data = malloc(width*height*3);
	if (!data) ExitWithError();
	for (int i = 0; i < width*height*3; i++) {
		data[i] = 128;
	}
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,data);
	*/

	focus.height = height, focus.width = width;
	focus.zoom = -1;

	glutMainLoop();

	pthread_mutex_destroy(&dispMutex);

	free(tbuf);
	free(fd1.data);
	free(fd2.data);
	return 0;
}
