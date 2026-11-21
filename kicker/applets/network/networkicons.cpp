/*
    Copyright (C) 2026 Serghei Amelian

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/

#include <math.h>

#include "networkicons.h"

// The icons are drawn by sampling each pixel 4 x 4 times: a shape says
// which of its colours (if any) is at a point of the unit square.

namespace
{

// Samples per pixel along each axis.
const int Samples = 4;

struct Color
{
    int r, g, b, a;
};

// Lit and unlit parts of the fan, the greyed out fan, the badges.
const Color Lit = { 0x1f, 0x52, 0xa0, 255 };
const Color Unlit = { 0x1f, 0x52, 0xa0, 70 };
const Color Grey = { 0x60, 0x60, 0x60, 110 };
const Color Plane = { 0x30, 0x30, 0x30, 255 };
const Color ErrorRed = { 0xd0, 0x20, 0x20, 255 };
const Color OffGrey = { 0x70, 0x70, 0x70, 255 };
const Color White = { 0xff, 0xff, 0xff, 255 };

class Shape
{
public:
    virtual ~Shape() {}
    /** The colour at (x, y) of the unit square, or 0 for none. */
    virtual const Color *at(double x, double y) const = 0;
};

QImage render(int size, const Shape &shape)
{
    QImage image(size, size, 32);
    image.setAlphaBuffer(true);

    const double step = 1.0 / (size * Samples);
    for (int py = 0; py < size; ++py)
    {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(py));
        for (int px = 0; px < size; ++px)
        {
            // premultiplied sums
            double r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < Samples; ++sy)
            {
                double y = (py * Samples + sy + 0.5) * step;
                for (int sx = 0; sx < Samples; ++sx)
                {
                    double x = (px * Samples + sx + 0.5) * step;
                    const Color *c = shape.at(x, y);
                    if (c)
                    {
                        r += c->r * c->a;
                        g += c->g * c->a;
                        b += c->b * c->a;
                        a += c->a;
                    }
                }
            }

            if (a <= 0)
            {
                line[px] = qRgba(0, 0, 0, 0);
                continue;
            }
            line[px] = qRgba(int(r / a + 0.5), int(g / a + 0.5), int(b / a + 0.5),
                             int(a / (Samples * Samples) + 0.5));
        }
    }

    return image;
}

// The fan: a dot and three arcs, a quarter circle wide, from a point
// near the bottom.
class Fan : public Shape
{
public:
    Fan(int bars, bool enabled) : m_bars(bars), m_enabled(enabled) {}

    virtual const Color *at(double x, double y) const
    {
        const double ox = 0.5, oy = 0.84;
        double dx = x - ox;
        double dy = oy - y;
        if (dy < 0)
        {
            return 0;
        }

        // within 45 degrees of the vertical
        if (fabs(dx) > dy)
        {
            return 0;
        }

        double r = sqrt(dx * dx + dy * dy);
        // the outer radius of the parts, and the gap before each
        static const double Outer[4] = { 0.20, 0.36, 0.52, 0.68 };
        static const double Gap = 0.06;
        for (int i = 0; i < 4; ++i)
        {
            double inner = i == 0 ? 0.0 : Outer[i - 1] + Gap;
            if (r >= inner && r <= Outer[i])
            {
                if (!m_enabled)
                {
                    return &Grey;
                }
                return i < m_bars ? &Lit : &Unlit;
            }
        }
        return 0;
    }

private:
    int m_bars;
    bool m_enabled;
};

class Airplane : public Shape
{
public:
    virtual const Color *at(double x, double y) const
    {
        // a plane seen from above, nose up (even-odd rule)
        static const double P[][2] = {
            { 0.50, 0.04 }, { 0.56, 0.11 }, { 0.56, 0.37 }, { 0.95, 0.60 },
            { 0.95, 0.69 }, { 0.56, 0.57 }, { 0.56, 0.80 }, { 0.69, 0.89 },
            { 0.69, 0.96 }, { 0.50, 0.91 }, { 0.31, 0.96 }, { 0.31, 0.89 },
            { 0.44, 0.80 }, { 0.44, 0.57 }, { 0.05, 0.69 }, { 0.05, 0.60 },
            { 0.44, 0.37 }, { 0.44, 0.11 }
        };
        const int n = sizeof(P) / sizeof(P[0]);

        bool inside = false;
        for (int i = 0, j = n - 1; i < n; j = i++)
        {
            if ((P[i][1] > y) != (P[j][1] > y) &&
                x < (P[j][0] - P[i][0]) * (y - P[i][1]) / (P[j][1] - P[i][1]) + P[i][0])
            {
                inside = !inside;
            }
        }
        return inside ? &Plane : 0;
    }
};

class BadgeShape : public Shape
{
public:
    BadgeShape(NetworkIcons::Badge badge) : m_badge(badge) {}

    virtual const Color *at(double x, double y) const
    {
        const double cx = 0.77, cy = 0.77, radius = 0.23;
        double dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy > radius * radius)
        {
            return 0;
        }

        const double half = 0.045;    // of the strokes' width
        const double arm = 0.12;
        if (m_badge == NetworkIcons::ErrorBadge)
        {
            // a cross: the two diagonals
            double d1 = fabs(dx - dy) / M_SQRT2;
            double d2 = fabs(dx + dy) / M_SQRT2;
            bool inArm = fabs(dx) <= arm && fabs(dy) <= arm;
            return inArm && (d1 <= half || d2 <= half) ? &White : &ErrorRed;
        }

        // a bar
        return fabs(dy) <= half && fabs(dx) <= arm ? &White : &OffGrey;
    }

private:
    NetworkIcons::Badge m_badge;
};

}

QImage NetworkIcons::wifi(int size, int bars, bool enabled, Badge badge)
{
    Fan fan(bars, enabled);
    return addBadge(render(size, fan), badge);
}

QImage NetworkIcons::airplane(int size)
{
    Airplane plane;
    return render(size, plane);
}

QImage NetworkIcons::addBadge(const QImage &image, Badge badge)
{
    if (badge == NoBadge || image.isNull())
    {
        return image;
    }

    QImage result = image.convertDepth(32);
    result.setAlphaBuffer(true);
    BadgeShape shape(badge);
    QImage top = render(QMIN(result.width(), result.height()), shape);

    // the badge over the image ("over" compositing)
    for (int y = 0; y < top.height(); ++y)
    {
        QRgb *dst = reinterpret_cast<QRgb *>(result.scanLine(y));
        const QRgb *src = reinterpret_cast<const QRgb *>(top.scanLine(y));
        for (int x = 0; x < top.width(); ++x)
        {
            int sa = qAlpha(src[x]);
            if (!sa)
            {
                continue;
            }
            int da = qAlpha(dst[x]);
            int oa = sa + da * (255 - sa) / 255;
            if (!oa)
            {
                continue;
            }
            int r = (qRed(src[x]) * sa + qRed(dst[x]) * da * (255 - sa) / 255) / oa;
            int g = (qGreen(src[x]) * sa + qGreen(dst[x]) * da * (255 - sa) / 255) / oa;
            int b = (qBlue(src[x]) * sa + qBlue(dst[x]) * da * (255 - sa) / 255) / oa;
            dst[x] = qRgba(r, g, b, oa);
        }
    }
    return result;
}

int NetworkIcons::bars(int strength)
{
    if (strength > 75)
    {
        return 4;
    }
    if (strength > 50)
    {
        return 3;
    }
    if (strength > 25)
    {
        return 2;
    }
    return 1;
}
