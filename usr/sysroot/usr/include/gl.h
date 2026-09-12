#ifndef _CERVUS_GL_H
#define _CERVUS_GL_H


#include <stdint.h>
#include <stddef.h>

typedef unsigned int  GLenum;
typedef unsigned int  GLbitfield;
typedef int           GLint;
typedef int           GLsizei;
typedef unsigned char GLboolean;
typedef float         GLfloat;
typedef double        GLdouble;
typedef void          GLvoid;

#define GL_FALSE 0
#define GL_TRUE  1

#define GL_POINTS         0x0000
#define GL_LINES          0x0001
#define GL_TRIANGLES      0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN   0x0006
#define GL_QUADS          0x0007
#define GL_QUAD_STRIP     0x0008
#define GL_POLYGON        0x0009

#define GL_MODELVIEW      0x1700
#define GL_PROJECTION     0x1701

#define GL_DEPTH_TEST     0x0B71
#define GL_CULL_FACE      0x0B44
#define GL_LIGHTING       0x0B50
#define GL_LIGHT0         0x4000
#define GL_LIGHT1         0x4001
#define GL_LIGHT2         0x4002
#define GL_LIGHT3         0x4003
#define GL_NORMALIZE      0x0BA1
#define GL_COLOR_MATERIAL 0x0B57

#define GL_FRONT          0x0404
#define GL_BACK           0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CW             0x0900
#define GL_CCW            0x0901

#define GL_FLAT           0x1D00
#define GL_SMOOTH         0x1D01

#define GL_AMBIENT             0x1200
#define GL_DIFFUSE             0x1201
#define GL_SPECULAR            0x1202
#define GL_POSITION            0x1203
#define GL_SHININESS           0x1601
#define GL_EMISSION            0x1600
#define GL_AMBIENT_AND_DIFFUSE 0x1602

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100

int   glCreateContext(int width, int height);
void  glDestroyContext(void);
const uint32_t *glColorBuffer(void);
int   glBufferWidth(void);
int   glBufferHeight(void);

void  glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void  glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void  glClear(GLbitfield mask);

void  glMatrixMode(GLenum mode);
void  glLoadIdentity(void);
void  glLoadMatrixf(const GLfloat *m);
void  glMultMatrixf(const GLfloat *m);
void  glPushMatrix(void);
void  glPopMatrix(void);
void  glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void  glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void  glScalef(GLfloat x, GLfloat y, GLfloat z);
void  glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void  glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void  gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zn, GLdouble zf);
void  gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez,
                GLdouble cx, GLdouble cy, GLdouble cz,
                GLdouble ux, GLdouble uy, GLdouble uz);

void  glEnable(GLenum cap);
void  glDisable(GLenum cap);
void  glShadeModel(GLenum mode);
void  glCullFace(GLenum mode);
void  glFrontFace(GLenum mode);

void  glLightfv(GLenum light, GLenum pname, const GLfloat *params);
void  glLightf(GLenum light, GLenum pname, GLfloat param);
void  glMaterialfv(GLenum face, GLenum pname, const GLfloat *params);
void  glMaterialf(GLenum face, GLenum pname, GLfloat param);

void  glBegin(GLenum mode);
void  glEnd(void);
void  glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void  glVertex3fv(const GLfloat *v);
void  glVertex2f(GLfloat x, GLfloat y);
void  glNormal3f(GLfloat x, GLfloat y, GLfloat z);
void  glNormal3fv(const GLfloat *v);
void  glColor3f(GLfloat r, GLfloat g, GLfloat b);
void  glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void  glColor3fv(const GLfloat *v);

void  glFlush(void);
void  glFinish(void);

long  glTrianglesDrawn(void);
void  glResetCounters(void);

void  glClearRegion(GLbitfield mask, int x0, int y0, int x1, int y1);
int   glDirtyRect(int *x0, int *y0, int *x1, int *y1);
void  glResetDirty(void);
const int *glDirtySpanMin(void);
const int *glDirtySpanMax(void);

#endif
