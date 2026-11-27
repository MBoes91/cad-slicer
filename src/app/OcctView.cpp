#include "app/OcctView.h"

#include <Aspect_DisplayConnection.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_DatumAspect.hxx>

#ifdef _WIN32
#include <WNT_Window.hxx>
#elif defined(__APPLE__)
#include <Cocoa_Window.hxx>
#else
#include <Xw_Window.hxx>
#endif

#include <QMouseEvent>
#include <QWheelEvent>

namespace {

Aspect_VKeyMouse toVKeyMouse(Qt::MouseButtons b)
{
    Aspect_VKeyMouse m = Aspect_VKeyMouse_NONE;
    if (b & Qt::LeftButton) m |= Aspect_VKeyMouse_LeftButton;
    if (b & Qt::MiddleButton) m |= Aspect_VKeyMouse_MiddleButton;
    if (b & Qt::RightButton) m |= Aspect_VKeyMouse_RightButton;
    return m;
}

Aspect_VKeyFlags toVKeyFlags(Qt::KeyboardModifiers mod)
{
    Aspect_VKeyFlags f = Aspect_VKeyFlags_NONE;
    if (mod & Qt::ShiftModifier) f |= Aspect_VKeyFlags_SHIFT;
    if (mod & Qt::ControlModifier) f |= Aspect_VKeyFlags_CTRL;
    if (mod & Qt::AltModifier) f |= Aspect_VKeyFlags_ALT;
    return f;
}

Graphic3d_Vec2i posOf(const QPointF& p, qreal dpr)
{
    return Graphic3d_Vec2i(static_cast<int>(p.x() * dpr), static_cast<int>(p.y() * dpr));
}

} // namespace

OcctView::OcctView(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NativeWindow);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(400, 300);

    // Mausbelegung
    AIS_MouseGestureMap& g = ChangeMouseGestureMap();
    g.Clear();
    g.Bind(Aspect_VKeyMouse_MiddleButton, AIS_MouseGesture_Pan);
    g.Bind(Aspect_VKeyMouse_MiddleButton | Aspect_VKeyFlags_SHIFT, AIS_MouseGesture_RotateOrbit);
    g.Bind(Aspect_VKeyMouse_RightButton, AIS_MouseGesture_RotateOrbit);
    g.Bind(Aspect_VKeyMouse_LeftButton | Aspect_VKeyFlags_SHIFT, AIS_MouseGesture_SelectRectangle);
    SetAllowRotation(true);
}

OcctView::~OcctView() = default;

void OcctView::initViewer()
{
    Handle(Aspect_DisplayConnection) disp = new Aspect_DisplayConnection();
    Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(disp, false);
    m_viewer = new V3d_Viewer(driver);
    m_viewer->SetDefaultLights();
    m_viewer->SetLightOn();
    m_viewer->SetDefaultTypeOfView(V3d_ORTHOGRAPHIC);

    m_view = m_viewer->CreateView();
    const auto wid = winId();
#ifdef _WIN32
    Handle(WNT_Window) win = new WNT_Window(reinterpret_cast<Aspect_Handle>(wid));
#elif defined(__APPLE__)
    Handle(Cocoa_Window) win = new Cocoa_Window(reinterpret_cast<NSView*>(wid));
#else
    Handle(Xw_Window) win = new Xw_Window(disp, static_cast<Aspect_Drawable>(wid));
#endif
    m_view->SetWindow(win);
    if (!win->IsMapped()) win->Map();

    // Heller Verlaufshintergrund, wie man ihn aus CAD-Programmen kennt
    m_view->SetBgGradientColors(Quantity_Color(0.62, 0.70, 0.80, Quantity_TOC_sRGB),
                                Quantity_Color(0.96, 0.97, 0.99, Quantity_TOC_sRGB),
                                Aspect_GradientFillMethod_Vertical);
    m_view->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, Quantity_NOC_GRAY20, 0.08, V3d_ZBUFFER);
    m_view->ChangeRenderingParams().IsAntialiasingEnabled = true;
    m_view->ChangeRenderingParams().NbMsaaSamples = 4;

    m_ctx = new AIS_InteractiveContext(m_viewer);
    m_ctx->SetDisplayMode(AIS_Shaded, false);
    m_ctx->SetPixelTolerance(4);

    // ViewCube mit deutschen Beschriftungen
    m_cube = new AIS_ViewCube();
    m_cube->SetBoxSideLabel(V3d_Xpos, "RECHTS");
    m_cube->SetBoxSideLabel(V3d_Xneg, "LINKS");
    m_cube->SetBoxSideLabel(V3d_Ypos, "HINTEN");
    m_cube->SetBoxSideLabel(V3d_Yneg, "VORNE");
    m_cube->SetBoxSideLabel(V3d_Zpos, "OBEN");
    m_cube->SetBoxSideLabel(V3d_Zneg, "UNTEN");
    m_cube->SetSize(55);
    m_cube->SetFontHeight(11);
    m_cube->SetBoxColor(Quantity_Color(0.88, 0.90, 0.93, Quantity_TOC_sRGB));
    m_cube->SetFixedAnimationLoop(false);
    m_cube->SetAutoStartAnimation(true);
    m_cube->SetViewAnimation(myViewAnimation);
    m_cube->SetTransformPersistence(new Graphic3d_TransformPers(
        Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER, Graphic3d_Vec2i(90, 90)));
    m_ctx->Display(m_cube, false);

    homeView();
}

void OcctView::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (m_view.IsNull()) initViewer();
}

void OcctView::paintEvent(QPaintEvent*)
{
    if (m_view.IsNull()) return;
    // Natives Fenster kann seine Groesse nach dem ersten Anzeigen noch aendern
    const QSize px = size() * devicePixelRatioF();
    if (px != m_viewSize) {
        m_viewSize = px;
        m_view->MustBeResized();
    }
    FlushViewEvents(m_ctx, m_view, true);
}

void OcctView::resizeEvent(QResizeEvent*)
{
    if (!m_view.IsNull()) {
        m_view->MustBeResized();
        update();
    }
}

void OcctView::handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx, const Handle(V3d_View)& view)
{
    AIS_ViewController::handleViewRedraw(ctx, view);
    if (toAskNextFrame()) update(); // Animation (z. B. ViewCube) weiterlaufen lassen
}

void OcctView::redraw()
{
    if (!m_view.IsNull()) {
        m_view->Invalidate();
        update();
    }
}

void OcctView::fitAll()
{
    if (m_view.IsNull()) return;
    m_view->FitAll(0.15, false);
    redraw();
}

void OcctView::homeView()
{
    // Isometrische Ausgangsansicht (von vorne-rechts-oben)
    setStandardView(V3d_TypeOfOrientation_Zup_AxoRight);
}

void OcctView::setStandardView(V3d_TypeOfOrientation o)
{
    if (m_view.IsNull()) return;
    m_view->SetProj(o);
    m_view->FitAll(0.15, false);
    redraw();
}

void OcctView::mousePressEvent(QMouseEvent* e)
{
    if (m_view.IsNull()) return;
    if (UpdateMouseButtons(posOf(e->position(), devicePixelRatioF()), toVKeyMouse(e->buttons()),
                           toVKeyFlags(e->modifiers()), false))
        update();
}

void OcctView::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_view.IsNull()) return;
    if (UpdateMouseButtons(posOf(e->position(), devicePixelRatioF()), toVKeyMouse(e->buttons()),
                           toVKeyFlags(e->modifiers()), false))
        update();
}

void OcctView::mouseMoveEvent(QMouseEvent* e)
{
    if (m_view.IsNull()) return;
    if (UpdateMousePosition(posOf(e->position(), devicePixelRatioF()), toVKeyMouse(e->buttons()),
                            toVKeyFlags(e->modifiers()), false))
        update();
}

void OcctView::wheelEvent(QWheelEvent* e)
{
    if (m_view.IsNull()) return;
    const Graphic3d_Vec2i pos = posOf(e->position(), devicePixelRatioF());
    if (UpdateZoom(Aspect_ScrollDelta(pos, e->angleDelta().y() / 8.0)))
        update();
}
