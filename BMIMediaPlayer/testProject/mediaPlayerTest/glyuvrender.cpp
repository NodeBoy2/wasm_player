#include "glyuvrender.h"

#include <QTime>
#include <QImage>
#include <QEvent>
#include <QRegion>
#include <QCoreApplication>
#include <QMutexLocker>
#include "frame.h"

#define ATTRIB_VERTEX 3
#define ATTRIB_TEXTURE 4

GLYUVRender::GLYUVRender(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_Front(nullptr)
    , m_Back(nullptr)
{

}

GLYUVRender::~GLYUVRender()
{
    clearRenderer();
}

unsigned char* GLYUVRender::getVideoFrameBuffer()
{
    if(m_Back)
        return m_Back->GetY();
    return nullptr;
}

void GLYUVRender::setVideoInfo(int &nWidth, int &nHeight, int &nFormat)
{
    clearRenderer();
    QMutexLocker locker(&m_imgMutex);
    m_Front = new I420Image(nWidth, nHeight);
    m_Back = new I420Image(nWidth, nHeight);
    nFormat = IFrame::FormatYUV420P;
    resizeGL(this->width(), this->height());
}

void GLYUVRender::createVideoFrameOver()
{
    m_imgMutex.lock();
    I420Image* p = m_Front;
    m_Front = m_Back;
    m_imgMutex.unlock();
    m_Back = p;
    updateUi();
}

void GLYUVRender::clearRenderer()
{
    m_imgMutex.lock();
    if(m_Front)
    {
        delete m_Front;
        m_Front = NULL;
    }
    m_imgMutex.unlock();
    if(m_Back)
    {
        delete m_Back;
        m_Back = NULL;
    }
    updateUi();
}

static const char *vertexShader = "\
attribute vec4 vertexIn;\
attribute vec2 textureIn;\
varying vec2 textureOut;\
uniform mat4 mWorld;\
uniform mat4 mView;\
uniform mat4 mProj;\
void main(void)\
{\
    gl_Position =vertexIn * mWorld * mView * mProj  ;\
    textureOut = textureIn;\
}";

static const char *fragmentShader = "\
varying vec2 textureOut;\
uniform sampler2D tex_y;\
uniform sampler2D tex_u;\
uniform sampler2D tex_v;\
void main(void)\
{\
    vec3 yuv;\
    vec3 rgb;\
    yuv.x = texture2D(tex_y, textureOut).r;\
    yuv.y = texture2D(tex_u, textureOut).r - 0.5;\
    yuv.z = texture2D(tex_v, textureOut).r - 0.5;\
    rgb = mat3( 1,       1,         1,\
                0,       -0.39465,  2.03211,\
                1.13983, -0.58060,  0) * yuv;\
    gl_FragColor = vec4(rgb, 1);\
}";

static const GLfloat vertexVertices[] = {
    -1.0f, -1.0f,
    1.0f, -1.0f,
    -1.0f,  1.0f,
    1.0f,  1.0f,
};

static const GLfloat textureVertices[] = {
    0.0f,  1.0f,
    1.0f,  1.0f,
    0.0f,  0.0f,
    1.0f,  0.0f,
};

void GLYUVRender::initializeGL()
{
    initializeOpenGLFunctions();
    InitShaders();
}

void GLYUVRender::paintGL()
{
    // 清除缓冲区
    glClearColor(0.0,0.0,0.0,0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    QMutexLocker locker(&m_imgMutex);
    if(m_Front)
    {
        int w = m_Front->GetWidth();
        int h = m_Front->GetHeight();
        // Y
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_y);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, (GLvoid*)m_Front->GetY());
        glUniform1i(sampler_y, 0);

        // U
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, tex_u);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w/2, h/2, 0, GL_RED, GL_UNSIGNED_BYTE, (GLvoid*)m_Front->GetU());
        glUniform1i(sampler_u, 1);

        // V
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, tex_v);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w/2, h/2, 0, GL_RED, GL_UNSIGNED_BYTE, (GLvoid*)m_Front->GetV());
        glUniform1i(sampler_v, 2);

//        QOpenGLShaderProgram::setUniformValue();
        glUniformMatrix4fv(matWorld,1, GL_FALSE,mWorld.constData());
        glUniformMatrix4fv(matView,1, GL_FALSE,mView.constData());
        glUniformMatrix4fv(matProj,1, GL_FALSE,mProj.constData());

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    }
    glFlush();
}

void GLYUVRender::resizeGL(int w, int h)
{
    float viewWidth=2.0f;
    float viewHeight=2.0f;

    mWorld.setToIdentity();

    mView.setToIdentity();
    mView.lookAt(QVector3D(0.0f,0.0f,1.0f),QVector3D(0.f,0.f,0.f),QVector3D(0.f,1.f,0.f));

    mProj.setToIdentity();
    if(m_Front)
    {
        float aspectRatio = float(m_Front->GetWidth()) / m_Front->GetHeight();
        //aspectRatio = float(4) / 3; // 强制长宽比
        if(float(float(w)/h > aspectRatio))
        {
            viewHeight = 2.0f;
            viewWidth = w*viewHeight / (aspectRatio * h);
        }
        else
        {
            viewWidth = 2.0f;
            viewHeight = h*viewWidth / (1/aspectRatio * w);
        }
    }

    mProj.ortho(-viewWidth/2,viewWidth/2,-viewHeight/2,viewHeight/2,-1.f,1.0f);
}

void GLYUVRender::updateUi()
{
    class QPaintLaterEvent : public QEvent
    {
        QRegion m_region;
    public:
        QPaintLaterEvent(QRegion region)
        : QEvent(QEvent::UpdateLater)
        , m_region(region)
        {}

        // inline QRegion region() const
        // {
        //     return m_region;
        // }
    };

    QCoreApplication::postEvent(this,new QPaintLaterEvent(QRegion(0,0,this->width(), this->height())));
}

void GLYUVRender::InitShaders()
{
    GLint vertCompiled, fragCompiled, linked;
    GLint v, f;

    //Shader: step1
    v = glCreateShader(GL_VERTEX_SHADER);
    f = glCreateShader(GL_FRAGMENT_SHADER);

    //Shader: step2
    glShaderSource(v, 1, &vertexShader,NULL);
    glShaderSource(f, 1, &fragmentShader,NULL);

    //Shader: step3
    glCompileShader(v);
    glGetShaderiv(v, GL_COMPILE_STATUS, &vertCompiled);    //Debug

    glCompileShader(f);
    glGetShaderiv(f, GL_COMPILE_STATUS, &fragCompiled);    //Debug

    //Program: Step1
    program = glCreateProgram();
    //Program: Step2
    glAttachShader(program,v);
    glAttachShader(program,f);

    glBindAttribLocation(program, ATTRIB_VERTEX, "vertexIn");
    glBindAttribLocation(program, ATTRIB_TEXTURE, "textureIn");
    //Program: Step3
    glLinkProgram(program);
    //Debug
    glGetProgramiv(program, GL_LINK_STATUS, &linked);

    glUseProgram(program);

    //Get Uniform Variables Location
    sampler_y = glGetUniformLocation(program, "tex_y");
    sampler_u = glGetUniformLocation(program, "tex_u");
    sampler_v = glGetUniformLocation(program, "tex_v");

    //Init Texture
    glGenTextures(1, &tex_y);
    glBindTexture(GL_TEXTURE_2D, tex_y);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &tex_u);
    glBindTexture(GL_TEXTURE_2D, tex_u);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &tex_v);
    glBindTexture(GL_TEXTURE_2D, tex_v);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glVertexAttribPointer(ATTRIB_VERTEX,2,GL_FLOAT,0,0,vertexVertices);
    glEnableVertexAttribArray(ATTRIB_VERTEX);

    glVertexAttribPointer(ATTRIB_TEXTURE,2,GL_FLOAT,0,0,textureVertices);
    glEnableVertexAttribArray(ATTRIB_TEXTURE);

    matWorld = glGetUniformLocation(program,"mWorld");
    matView = glGetUniformLocation(program,"mView");
    matProj = glGetUniformLocation(program,"mProj");
}

