#include "app/OcctView.h"

#include <Aspect_DisplayConnection.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_Vec2.hxx>
#include <AIS_MouseGesture.hxx>

#include <algorithm>
#include <cmath>
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

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
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
    auto& g = ChangeMouseGestureMap(); // Typname unterscheidet sich zwischen OCCT 7 und 8
    g.Clear();
    g.Bind(Aspect_VKeyMouse_MiddleButton, AIS_MouseGesture_Pan);
    g.Bind(Aspect_VKeyMouse_MiddleButton | Aspect_VKeyFlags_SHIFT, AIS_MouseGesture_RotateOrbit);
    // Zoom per Ziehen (F3) wird intern auf diese Kombination umgelegt
    g.Bind(Aspect_VKeyMouse_RightButton | Aspect_VKeyFlags_CTRL | Aspect_VKeyFlags_ALT, AIS_MouseGesture_Zoom);
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
    if (m_fitPending) {
        m_fitPending = false;
        m_view->FitAll(0.15, false);
        m_view->Invalidate();
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
    // Erst beim naechsten Zeichnen ausfuehren, wenn die Fenstergroesse sicher stimmt
    if (!m_view.IsNull()) rememberCurrent();
    m_fitPending = true;
    update();
}

void OcctView::homeView()
{
    // Isometrische Ausgangsansicht (von vorne-rechts-oben)
    setStandardView(V3d_TypeOfOrientation_Zup_AxoRight);
}

void OcctView::setStandardView(V3d_TypeOfOrientation o)
{
    if (m_view.IsNull()) return;
    rememberCurrent();
    m_view->SetProj(o);
    fitAll();
}

void OcctView::setPerspective(bool on)
{
    if (m_view.IsNull()) return;
    m_view->Camera()->SetProjectionType(on ? Graphic3d_Camera::Projection_Perspective
                                           : Graphic3d_Camera::Projection_Orthographic);
    redraw();
}

// --- Kamera-Verlauf (F5) -----------------------------------------------------------

namespace {
bool sameCamera(const Handle(Graphic3d_Camera)& a, const Handle(Graphic3d_Camera)& b)
{
    if (a.IsNull() || b.IsNull()) return false;
    const double tol = 1e-6 * std::max(1.0, a->Distance());
    return a->Eye().Distance(b->Eye()) < tol && a->Center().Distance(b->Center()) < tol
        && a->Up().IsEqual(b->Up(), 1e-9) && std::abs(a->Scale() - b->Scale()) <= 1e-9 * std::abs(a->Scale())
        && a->ProjectionType() == b->ProjectionType();
}
} // namespace

Handle(Graphic3d_Camera) OcctView::snapshot() const
{
    Handle(Graphic3d_Camera) c = new Graphic3d_Camera();
    c->Copy(m_view->Camera());
    return c;
}

void OcctView::remember(const Handle(Graphic3d_Camera)& cam)
{
    if (cam.IsNull()) return;
    if (!m_history.empty() && sameCamera(m_history.back(), cam)) return;
    m_history.push_back(cam);
    if (m_history.size() > 50) m_history.erase(m_history.begin());
}

void OcctView::rememberCurrent()
{
    if (!m_view.IsNull()) remember(snapshot());
}

bool OcctView::hasPreviousView() const
{
    return !m_history.empty();
}

void OcctView::previousView()
{
    if (m_view.IsNull()) return;
    const Handle(Graphic3d_Camera) now = snapshot();
    while (!m_history.empty() && sameCamera(m_history.back(), now)) m_history.pop_back();
    if (m_history.empty()) {
        emit hint(QString::fromUtf8("Keine vorherige Ansicht vorhanden"));
        return;
    }
    m_view->Camera()->Copy(m_history.back());
    m_history.pop_back();
    m_view->Invalidate();
    update();
}

// --- Navigationsmodi (F2/F3/F4) ------------------------------------------------------

void OcctView::setNavMode(NavMode mode, bool sticky)
{
    m_navMode = mode;
    m_navSticky = sticky && mode != NavMode::None;
    switch (mode) {
    case NavMode::Pan:
        setCursor(Qt::OpenHandCursor);
        emit hint(QString::fromUtf8("Verschieben: mit gedrückter linker Maustaste ziehen") + (m_navSticky ? QString::fromUtf8(" · Esc beendet") : QString()));
        break;
    case NavMode::Zoom:
        setCursor(Qt::SizeVerCursor);
        emit hint(QString::fromUtf8("Zoom: mit gedrückter linker Maustaste nach oben/unten ziehen") + (m_navSticky ? QString::fromUtf8(" · Esc beendet") : QString()));
        break;
    case NavMode::Orbit:
        setCursor(Qt::SizeAllCursor);
        emit hint(QString::fromUtf8("Orbit: mit gedrückter linker Maustaste ziehen") + (m_navSticky ? QString::fromUtf8(" · Esc beendet") : QString()));
        break;
    case NavMode::None:
        unsetCursor();
        emit hint(QString());
        break;
    }
}

Aspect_VKeyMouse OcctView::mapButtons(Qt::MouseButtons b) const
{
    Qt::MouseButtons rest = b;
    Aspect_VKeyMouse m = Aspect_VKeyMouse_NONE;
    if (m_navMode != NavMode::None && (b & Qt::LeftButton)) {
        rest &= ~Qt::LeftButton;
        m |= (m_navMode == NavMode::Zoom) ? Aspect_VKeyMouse_RightButton : Aspect_VKeyMouse_MiddleButton;
    }
    // Die rechte Taste bewegt die Ansicht nicht (sie oeffnet das Kontextmenue)
    rest &= ~Qt::RightButton;
    return m | toVKeyMouse(rest);
}

Aspect_VKeyFlags OcctView::mapFlags(Qt::MouseButtons b, Qt::KeyboardModifiers mod) const
{
    if (m_navMode != NavMode::None && (b & Qt::LeftButton)) {
        if (m_navMode == NavMode::Orbit) return Aspect_VKeyFlags_SHIFT;
        if (m_navMode == NavMode::Zoom) return Aspect_VKeyFlags_CTRL | Aspect_VKeyFlags_ALT;
        return Aspect_VKeyFlags_NONE;
    }
    return toVKeyFlags(mod);
}

QPointF OcctView::project(const gp_Pnt& p) const
{
    if (m_view.IsNull()) return {};
    const gp_Pnt ndc = m_view->Camera()->Project(p);
    Standard_Integer w = 0, h = 0;
    m_view->Window()->Size(w, h);
    return QPointF((ndc.X() + 1.0) * 0.5 * w, (1.0 - ndc.Y()) * 0.5 * h);
}

void OcctView::mousePressEvent(QMouseEvent* e)
{
    if (m_view.IsNull()) return;
    if (e->button() == Qt::LeftButton && m_navMode == NavMode::None && m_drag.begin) {
        const Graphic3d_Vec2i p = posOf(e->position(), devicePixelRatioF());
        m_ctx->MoveTo(p.x(), p.y(), m_view, false);
        if (m_ctx->HasDetected() && m_drag.begin(m_ctx->DetectedInteractive())) {
            m_dragActive = true;
            m_dragStart = QPointF(p.x(), p.y());
            return;
        }
    }
    if (m_lastButtons == Qt::NoButton) m_pressCamera = snapshot();
    m_lastButtons = e->buttons();
    if (e->button() == Qt::RightButton) {
        if (m_navSticky) { setNavMode(NavMode::None, false); return; }
        emit contextMenuAt(e->globalPosition().toPoint());
        return;
    }
    if (m_navMode == NavMode::Pan && e->button() == Qt::LeftButton) setCursor(Qt::ClosedHandCursor);
    if (UpdateMouseButtons(posOf(e->position(), devicePixelRatioF()), mapButtons(e->buttons()),
                           mapFlags(e->buttons(), e->modifiers()), false))
        update();
}

void OcctView::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_view.IsNull()) return;
    if (m_dragActive) {
        m_dragActive = false;
        if (m_drag.end) m_drag.end();
        update();
        return;
    }
    m_lastButtons = e->buttons();
    if (m_navMode == NavMode::Pan && e->button() == Qt::LeftButton) setCursor(Qt::OpenHandCursor);
    if (UpdateMouseButtons(posOf(e->position(), devicePixelRatioF()), mapButtons(e->buttons()),
                           mapFlags(e->buttons(), e->modifiers()), false))
        update();
    if (e->buttons() == Qt::NoButton && !m_pressCamera.IsNull()) {
        // Nach Ende der Bewegung (auch ViewCube-Animation) die Ansicht davor merken
        Handle(Graphic3d_Camera) before = m_pressCamera;
        m_pressCamera.Nullify();
        QTimer::singleShot(700, this, [this, before] {
            if (!m_view.IsNull() && !sameCamera(before, m_view->Camera())) remember(before);
        });
    }
}

void OcctView::mouseMoveEvent(QMouseEvent* e)
{
    if (m_view.IsNull()) return;
    if (m_dragActive) {
        const Graphic3d_Vec2i p = posOf(e->position(), devicePixelRatioF());
        if (m_drag.move) m_drag.move(m_dragStart, QPointF(p.x(), p.y()));
        update();
        return;
    }
    if (UpdateMousePosition(posOf(e->position(), devicePixelRatioF()), mapButtons(e->buttons()),
                            mapFlags(e->buttons(), e->modifiers()), false))
        update();
}

void OcctView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton) {
        fitAll();
        return;
    }
    mousePressEvent(e);
}

void OcctView::wheelEvent(QWheelEvent* e)
{
    if (m_view.IsNull()) return;
    // Eine zusammenhaengende Zoom-Bewegung zaehlt als ein Schritt im Verlauf
    if (!m_wheelTimer.isValid() || m_wheelTimer.elapsed() > 800) rememberCurrent();
    m_wheelTimer.restart();
    const Graphic3d_Vec2i pos = posOf(e->position(), devicePixelRatioF());
    if (UpdateZoom(Aspect_ScrollDelta(pos, e->angleDelta().y() / 8.0)))
        update();
}

void OcctView::keyPressEvent(QKeyEvent* e)
{
    if (e->isAutoRepeat()) { e->accept(); return; }
    switch (e->key()) {
    case Qt::Key_F2: setNavMode(NavMode::Pan, false); break;
    case Qt::Key_F3: setNavMode(NavMode::Zoom, false); break;
    case Qt::Key_F4: setNavMode(NavMode::Orbit, false); break;
    case Qt::Key_Escape:
        if (m_navMode != NavMode::None) { setNavMode(NavMode::None, false); break; }
        QWidget::keyPressEvent(e);
        return;
    default:
        QWidget::keyPressEvent(e);
        return;
    }
    e->accept();
}

void OcctView::keyReleaseEvent(QKeyEvent* e)
{
    if (e->isAutoRepeat()) { e->accept(); return; }
    const int k = e->key();
    if ((k == Qt::Key_F2 || k == Qt::Key_F3 || k == Qt::Key_F4) && !m_navSticky) {
        setNavMode(NavMode::None, false);
        e->accept();
        return;
    }
    QWidget::keyReleaseEvent(e);
}

void OcctView::enterEvent(QEnterEvent* e)
{
    // Wie in Inventor: F-Tasten wirken, sobald die Maus ueber dem Grafikfenster ist
    if (window()->isActiveWindow() && !QApplication::activePopupWidget()) setFocus(Qt::MouseFocusReason);
    QWidget::enterEvent(e);
}
