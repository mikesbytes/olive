#include "openglworker.h"

#include "node/node.h"

OpenGLWorker::OpenGLWorker(QOpenGLContext *share_ctx, QObject *parent) :
  QObject(parent),
  share_ctx_(share_ctx),
  ctx_(nullptr),
  functions_(nullptr)
{
  surface_.create();
}

OpenGLWorker::~OpenGLWorker()
{
  surface_.destroy();
}

bool OpenGLWorker::IsStarted()
{
  return ctx_ != nullptr;
}

void OpenGLWorker::SetParameters(const VideoRenderingParams &video_params)
{
  video_params_ = video_params;
}

void OpenGLWorker::Init()
{
  // Create context object
  ctx_ = new QOpenGLContext();

  // Set share context
  ctx_->setShareContext(share_ctx_);

  // Create OpenGL context (automatically destroys any existing if there is one)
  if (!ctx_->create()) {
    qWarning() << "Failed to create OpenGL context in thread" << thread();
    Close();
    return;
  }

  ctx_->moveToThread(this->thread());

  qDebug() << "Processor initialized in thread" << thread() << "- context is in" << ctx_->thread();

  // The rest of the initialization needs to occur in the other thread, so we signal for it to start
  QMetaObject::invokeMethod(this, "FinishInit", Qt::QueuedConnection);
}

void OpenGLWorker::Close()
{
  buffer_.Destroy();

  functions_ = nullptr;
  delete ctx_;
}

void OpenGLWorker::Render(const NodeDependency &path)
{
  NodeOutput* output = path.node();
  Node* node = output->parent();

  QList<Node*> all_nodes_in_graph;
  all_nodes_in_graph.append(node);
  all_nodes_in_graph.append(node->GetDependencies());

  // Lock all Nodes to prevent UI changes during this render
  foreach (Node* dep, all_nodes_in_graph) {
    dep->LockUserInput();
  }

  // Start traversing graph
  RenderAsSibling(path);

  // Start OpenGL flushing now while we do clean up work on the CPU
  functions_->glFlush();

  // Unlock all Nodes so changes can be made again
  foreach (Node* dep, all_nodes_in_graph) {
    dep->UnlockUserInput();
  }

  // Now we need the texture done so we call glFinish()
  functions_->glFinish();
}

void OpenGLWorker::UpdateViewportFromParams()
{
  if (functions_ != nullptr && video_params_.is_valid()) {
    functions_->glViewport(0, 0, video_params_.effective_width(), video_params_.effective_height());
  }
}

void OpenGLWorker::FinishInit()
{
  // Make context current on that surface
  if (!ctx_->makeCurrent(&surface_)) {
    qWarning() << "Failed to makeCurrent() on offscreen surface in thread" << thread();
    return;
  }

  // Store OpenGL functions instance
  functions_ = ctx_->functions();

  // Set up OpenGL parameters as necessary
  functions_->glEnable(GL_BLEND);
  UpdateViewportFromParams();

  buffer_.Create(ctx_);

  qDebug() << "Context in" << ctx_->thread() << "successfully finished";
}

void OpenGLWorker::RenderAsSibling(const NodeDependency &dep)
{
  NodeOutput* output = dep.node();
  Node* node = output->parent();

  node->LockProcessing();

  // Check if block
  //   If yes, traverse previous and next until we find the right block
  // Check all inputs for ones that are dependents
  //   If input is NOT connected, set stored value to keyframe at time
  //     Some inputs we handle ourselves such as FOOTAGE
  //   If input IS connected, we need to traverse down it
  //   If more than one input is connected, signal out for a sibling to handle it
  //   Keep iterating inputs until all are up to date
  // Finally get value from output
  //   If we have shader code, the output is a texture and we handle the I/O
  //   If we don't, the output is some other value and the Node will produce the output

  node->UnlockProcessing();
}