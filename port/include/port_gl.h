/* The front end's OpenGL calls (win/main.c, win/glfx.c) renamed to port_gl*:
 * with PS4_GLES they go to port/gles.c (OpenGL ES 2, with the few desktop
 * GL 1.1 calls emulated), with PS4_NO_GL to empty stand-ins. */
#ifndef PORT_GL_H
#define PORT_GL_H
#if (defined(PS4_GLES) || defined(PS4_NO_GL)) && !defined(PORT_GL_IMPL)
#define glViewport port_glViewport
#define glClearColor port_glClearColor
#define glClear port_glClear
#define glGenTextures port_glGenTextures
#define glBindTexture port_glBindTexture
#define glTexParameteri port_glTexParameteri
#define glTexImage2D port_glTexImage2D
#define glTexSubImage2D port_glTexSubImage2D
#define glEnable port_glEnable
#define glDisable port_glDisable
#define glBlendFunc port_glBlendFunc
#define glBegin port_glBegin
#define glEnd port_glEnd
#define glTexCoord2f port_glTexCoord2f
#define glVertex2f port_glVertex2f
#define glColor4f port_glColor4f
#define glMatrixMode port_glMatrixMode
#define glLoadIdentity port_glLoadIdentity
#define glOrtho port_glOrtho
#define glPixelStorei port_glPixelStorei
#define glGetString port_glGetString
#define glGetError port_glGetError
#endif
#endif
