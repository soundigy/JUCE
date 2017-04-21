/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2015 - ROLI Ltd.

   Permission is granted to use this software under the terms of either:
   a) the GPL v2 (or any later version)
   b) the Affero GPL v3

   Details of these licenses can be found at: www.gnu.org/licenses

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

   ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.juce.com for more information.

  ==============================================================================
*/

class SVGState
{
public:
    //==============================================================================
    explicit SVGState (const XmlElement* topLevel)  : topLevelXml (topLevel, nullptr)
    {
    }

    struct XmlPath
    {
        XmlPath (const XmlElement* e, const XmlPath* p) noexcept : xml (e), parent (p)  {}

        const XmlElement& operator*() const noexcept            { jassert (xml != nullptr); return *xml; }
        const XmlElement* operator->() const noexcept           { return xml; }
        XmlPath getChild (const XmlElement* e) const noexcept   { return XmlPath (e, this); }

        template <typename OperationType>
        bool applyOperationToChildWithID (const String& id, OperationType& op) const
        {
            forEachXmlChildElement (*xml, e)
            {
                XmlPath child (e, this);

                if (e->compareAttribute ("id", id))
                {
                    op (child);
                    return true;
                }

                if (child.applyOperationToChildWithID (id, op))
                    return true;
            }

            return false;
        }

        const XmlElement* xml;
        const XmlPath* parent;
    };

    //==============================================================================
    struct UsePathOp
    {
        const SVGState* state;
        Path* targetPath;

        void operator() (const XmlPath& xmlPath) const
        {
            state->parsePathElement (xmlPath, *targetPath);
        }
    };

    struct GetClipPathOp
    {
        const SVGState* state;
        Drawable* target;

        void operator() (const XmlPath& xmlPath) const
        {
            state->applyClipPath (*target, xmlPath);
        }
    };

    struct SetGradientStopsOp
    {
        const SVGState* state;
        ColourGradient* gradient;

        void operator() (const XmlPath& xml) const
        {
            state->addGradientStopsIn (*gradient, xml);
        }
    };

    struct GetFillTypeOp
    {
        const SVGState* state;
        const Path* path;
        float opacity;
        FillType fillType;

        void operator() (const XmlPath& xml)
        {
            if (xml->hasTagNameIgnoringNamespace ("linearGradient")
                 || xml->hasTagNameIgnoringNamespace ("radialGradient"))
                fillType = state->getGradientFillType (xml, *path, opacity);
        }
    };

    //==============================================================================
    Drawable* parseSVGElement (const XmlPath& xml)
    {
        auto drawable = new DrawableComposite();
        setCommonAttributes (*drawable, xml);

        SVGState newState (*this);

        if (xml->hasAttribute ("transform"))
            newState.addTransform (xml);

        newState.width  = getCoordLength (xml->getStringAttribute ("width",  String (newState.width)),  viewBoxW);
        newState.height = getCoordLength (xml->getStringAttribute ("height", String (newState.height)), viewBoxH);

        if (newState.width  <= 0) newState.width  = 100;
        if (newState.height <= 0) newState.height = 100;

        Point<float> viewboxXY;

        if (xml->hasAttribute ("viewBox"))
        {
            auto viewBoxAtt = xml->getStringAttribute ("viewBox");
            auto viewParams = viewBoxAtt.getCharPointer();
            Point<float> vwh;

            if (parseCoords (viewParams, viewboxXY, true)
                 && parseCoords (viewParams, vwh, true)
                 && vwh.x > 0
                 && vwh.y > 0)
            {
                newState.viewBoxW = vwh.x;
                newState.viewBoxH = vwh.y;

                auto placementFlags = parsePlacementFlags (xml->getStringAttribute ("preserveAspectRatio").trim());

                if (placementFlags != 0)
                    newState.transform = RectanglePlacement (placementFlags)
                                            .getTransformToFit (Rectangle<float> (viewboxXY.x, viewboxXY.y, vwh.x, vwh.y),
                                                                Rectangle<float> (newState.width, newState.height))
                                            .followedBy (newState.transform);
            }
        }
        else
        {
            if (viewBoxW == 0.0f)  newState.viewBoxW = newState.width;
            if (viewBoxH == 0.0f)  newState.viewBoxH = newState.height;
        }

        newState.parseSubElements (xml, *drawable);

        drawable->setContentArea (RelativeRectangle (RelativeCoordinate (viewboxXY.x),
                                                     RelativeCoordinate (viewboxXY.x + newState.viewBoxW),
                                                     RelativeCoordinate (viewboxXY.y),
                                                     RelativeCoordinate (viewboxXY.y + newState.viewBoxH)));
        drawable->resetBoundingBoxToContentArea();

        return drawable;
    }

    //==============================================================================
    void parsePathString (Path& path, const String& pathString) const
    {
        auto d = pathString.getCharPointer().findEndOfWhitespace();

        Point<float> subpathStart, last, last2, p1, p2, p3;
        juce_wchar currentCommand = 0, previousCommand = 0;
        bool isRelative = true;
        bool carryOn = true;

        while (! d.isEmpty())
        {
            if (CharPointer_ASCII ("MmLlHhVvCcSsQqTtAaZz").indexOf (*d) >= 0)
            {
                currentCommand = d.getAndAdvance();
                isRelative = currentCommand >= 'a';
            }

            switch (currentCommand)
            {
            case 'M':
            case 'm':
            case 'L':
            case 'l':
                if (parseCoordsOrSkip (d, p1, false))
                {
                    if (isRelative)
                        p1 += last;

                    if (currentCommand == 'M' || currentCommand == 'm')
                    {
                        subpathStart = p1;
                        path.startNewSubPath (p1);
                        currentCommand = 'l';
                    }
                    else
                        path.lineTo (p1);

                    last2 = last;
                    last = p1;
                }
                break;

            case 'H':
            case 'h':
                if (parseCoord (d, p1.x, false, true))
                {
                    if (isRelative)
                        p1.x += last.x;

                    path.lineTo (p1.x, last.y);

                    last2.x = last.x;
                    last.x = p1.x;
                }
                else
                {
                    ++d;
                }
                break;

            case 'V':
            case 'v':
                if (parseCoord (d, p1.y, false, false))
                {
                    if (isRelative)
                        p1.y += last.y;

                    path.lineTo (last.x, p1.y);

                    last2.y = last.y;
                    last.y = p1.y;
                }
                else
                {
                    ++d;
                }
                break;

            case 'C':
            case 'c':
                if (parseCoordsOrSkip (d, p1, false)
                     && parseCoordsOrSkip (d, p2, false)
                     && parseCoordsOrSkip (d, p3, false))
                {
                    if (isRelative)
                    {
                        p1 += last;
                        p2 += last;
                        p3 += last;
                    }

                    path.cubicTo (p1, p2, p3);

                    last2 = p2;
                    last = p3;
                }
                break;

            case 'S':
            case 's':
                if (parseCoordsOrSkip (d, p1, false)
                     && parseCoordsOrSkip (d, p3, false))
                {
                    if (isRelative)
                    {
                        p1 += last;
                        p3 += last;
                    }

                    p2 = last + (last - last2);
                    path.cubicTo (p2, p1, p3);

                    last2 = p1;
                    last = p3;
                }
                break;

            case 'Q':
            case 'q':
                if (parseCoordsOrSkip (d, p1, false)
                     && parseCoordsOrSkip (d, p2, false))
                {
                    if (isRelative)
                    {
                        p1 += last;
                        p2 += last;
                    }

                    path.quadraticTo (p1, p2);

                    last2 = p1;
                    last = p2;
                }
                break;

            case 'T':
            case 't':
                if (parseCoordsOrSkip (d, p1, false))
                {
                    if (isRelative)
                        p1 += last;

                    p2 = CharPointer_ASCII ("QqTt").indexOf (previousCommand) >= 0 ? last + (last - last2)
                                                                                   : p1;
                    path.quadraticTo (p2, p1);

                    last2 = p2;
                    last = p1;
                }
                break;

            case 'A':
            case 'a':
                if (parseCoordsOrSkip (d, p1, false))
                {
                    String num;

                    if (parseNextNumber (d, num, false))
                    {
                        const float angle = degreesToRadians (num.getFloatValue());

                        if (parseNextNumber (d, num, false))
                        {
                            const bool largeArc = num.getIntValue() != 0;

                            if (parseNextNumber (d, num, false))
                            {
                                const bool sweep = num.getIntValue() != 0;

                                if (parseCoordsOrSkip (d, p2, false))
                                {
                                    if (isRelative)
                                        p2 += last;

                                    if (last != p2)
                                    {
                                        double centreX, centreY, startAngle, deltaAngle;
                                        double rx = p1.x, ry = p1.y;

                                        endpointToCentreParameters (last.x, last.y, p2.x, p2.y,
                                                                    angle, largeArc, sweep,
                                                                    rx, ry, centreX, centreY,
                                                                    startAngle, deltaAngle);

                                        path.addCentredArc ((float) centreX, (float) centreY,
                                                            (float) rx, (float) ry,
                                                            angle, (float) startAngle, (float) (startAngle + deltaAngle),
                                                            false);

                                        path.lineTo (p2);
                                    }

                                    last2 = last;
                                    last = p2;
                                }
                            }
                        }
                    }
                }

                break;

            case 'Z':
            case 'z':
                path.closeSubPath();
                last = last2 = subpathStart;
                d = d.findEndOfWhitespace();
                currentCommand = 'M';
                break;

            default:
                carryOn = false;
                break;
            }

            if (! carryOn)
                break;

            previousCommand = currentCommand;
        }

        // paths that finish back at their start position often seem to be
        // left without a 'z', so need to be closed explicitly..
        if (path.getCurrentPosition() == subpathStart)
            path.closeSubPath();
    }

private:
    //==============================================================================
    const XmlPath topLevelXml;
    float width = 512, height = 512, viewBoxW = 0, viewBoxH = 0;
    AffineTransform transform;
    String cssStyleText;

    static void setCommonAttributes (Drawable& d, const XmlPath& xml)
    {
        auto compID = xml->getStringAttribute ("id");
        d.setName (compID);
        d.setComponentID (compID);

        if (xml->getStringAttribute ("display") == "none")
            d.setVisible (false);
    }

    //==============================================================================
    void parseSubElements (const XmlPath& xml, DrawableComposite& parentDrawable)
    {
        forEachXmlChildElement (*xml, e)
            parentDrawable.addAndMakeVisible (parseSubElement (xml.getChild (e)));
    }

    Drawable* parseSubElement (const XmlPath& xml)
    {
        {
            Path path;
            if (parsePathElement (xml, path))
                return parseShape (xml, path);
        }

        auto tag = xml->getTagNameWithoutNamespace();

        if (tag == "g")         return parseGroupElement (xml);
        if (tag == "svg")       return parseSVGElement (xml);
        if (tag == "text")      return parseText (xml, true);
        if (tag == "switch")    return parseSwitch (xml);
        if (tag == "a")         return parseLinkElement (xml);
        if (tag == "style")     parseCSSStyle (xml);
        if (tag == "defs")      parseDefs (xml);

        return nullptr;
    }

    bool parsePathElement (const XmlPath& xml, Path& path) const
    {
        const String tag (xml->getTagNameWithoutNamespace());

        if (tag == "path")      { parsePath (xml, path);           return true; }
        if (tag == "rect")      { parseRect (xml, path);           return true; }
        if (tag == "circle")    { parseCircle (xml, path);         return true; }
        if (tag == "ellipse")   { parseEllipse (xml, path);        return true; }
        if (tag == "line")      { parseLine (xml, path);           return true; }
        if (tag == "polyline")  { parsePolygon (xml, true, path);  return true; }
        if (tag == "polygon")   { parsePolygon (xml, false, path); return true; }
        if (tag == "use")       { parseUse (xml, path);            return true; }

        return false;
    }

    DrawableComposite* parseSwitch (const XmlPath& xml)
    {
        if (auto* group = xml->getChildByName ("g"))
            return parseGroupElement (xml.getChild (group));

        return nullptr;
    }

    DrawableComposite* parseGroupElement (const XmlPath& xml)
    {
        auto drawable = new DrawableComposite();

        setCommonAttributes (*drawable, xml);

        if (xml->hasAttribute ("transform"))
        {
            SVGState newState (*this);
            newState.addTransform (xml);
            newState.parseSubElements (xml, *drawable);
        }
        else
        {
            parseSubElements (xml, *drawable);
        }

        drawable->resetContentAreaAndBoundingBoxToFitChildren();
        return drawable;
    }

    DrawableComposite* parseLinkElement (const XmlPath& xml)
    {
        return parseGroupElement (xml); // TODO: support for making this clickable
    }

    //==============================================================================
    void parsePath (const XmlPath& xml, Path& path) const
    {
        parsePathString (path, xml->getStringAttribute ("d"));

        if (getStyleAttribute (xml, "fill-rule").trim().equalsIgnoreCase ("evenodd"))
            path.setUsingNonZeroWinding (false);
    }

    void parseRect (const XmlPath& xml, Path& rect) const
    {
        const bool hasRX = xml->hasAttribute ("rx");
        const bool hasRY = xml->hasAttribute ("ry");

        if (hasRX || hasRY)
        {
            float rx = getCoordLength (xml, "rx", viewBoxW);
            float ry = getCoordLength (xml, "ry", viewBoxH);

            if (! hasRX)
                rx = ry;
            else if (! hasRY)
                ry = rx;

            rect.addRoundedRectangle (getCoordLength (xml, "x", viewBoxW),
                                      getCoordLength (xml, "y", viewBoxH),
                                      getCoordLength (xml, "width", viewBoxW),
                                      getCoordLength (xml, "height", viewBoxH),
                                      rx, ry);
        }
        else
        {
            rect.addRectangle (getCoordLength (xml, "x", viewBoxW),
                               getCoordLength (xml, "y", viewBoxH),
                               getCoordLength (xml, "width", viewBoxW),
                               getCoordLength (xml, "height", viewBoxH));
        }
    }

    void parseCircle (const XmlPath& xml, Path& circle) const
    {
        const float cx = getCoordLength (xml, "cx", viewBoxW);
        const float cy = getCoordLength (xml, "cy", viewBoxH);
        const float radius = getCoordLength (xml, "r", viewBoxW);

        circle.addEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
    }

    void parseEllipse (const XmlPath& xml, Path& ellipse) const
    {
        const float cx      = getCoordLength (xml, "cx", viewBoxW);
        const float cy      = getCoordLength (xml, "cy", viewBoxH);
        const float radiusX = getCoordLength (xml, "rx", viewBoxW);
        const float radiusY = getCoordLength (xml, "ry", viewBoxH);

        ellipse.addEllipse (cx - radiusX, cy - radiusY, radiusX * 2.0f, radiusY * 2.0f);
    }

    void parseLine (const XmlPath& xml, Path& line) const
    {
        const float x1 = getCoordLength (xml, "x1", viewBoxW);
        const float y1 = getCoordLength (xml, "y1", viewBoxH);
        const float x2 = getCoordLength (xml, "x2", viewBoxW);
        const float y2 = getCoordLength (xml, "y2", viewBoxH);

        line.startNewSubPath (x1, y1);
        line.lineTo (x2, y2);
    }

    void parsePolygon (const XmlPath& xml, const bool isPolyline, Path& path) const
    {
        auto pointsAtt = xml->getStringAttribute ("points");
        auto points = pointsAtt.getCharPointer();
        Point<float> p;

        if (parseCoords (points, p, true))
        {
            Point<float> first (p), last;

            path.startNewSubPath (first);

            while (parseCoords (points, p, true))
            {
                last = p;
                path.lineTo (p);
            }

            if ((! isPolyline) || first == last)
                path.closeSubPath();
        }
    }

    void parseUse (const XmlPath& xml, Path& path) const
    {
        auto link = xml->getStringAttribute ("xlink:href");

        if (link.startsWithChar ('#'))
        {
            auto linkedID = link.substring (1);

            UsePathOp op = { this, &path };
            topLevelXml.applyOperationToChildWithID (linkedID, op);
        }
    }

    static String parseURL (const String& str)
    {
        if (str.startsWithIgnoreCase ("url"))
            return str.fromFirstOccurrenceOf ("#", false, false)
                      .upToLastOccurrenceOf (")", false, false).trim();

        return {};
    }

    //==============================================================================
    Drawable* parseShape (const XmlPath& xml, Path& path,
                          const bool shouldParseTransform = true) const
    {
        if (shouldParseTransform && xml->hasAttribute ("transform"))
        {
            SVGState newState (*this);
            newState.addTransform (xml);

            return newState.parseShape (xml, path, false);
        }

        auto dp = new DrawablePath();
        setCommonAttributes (*dp, xml);
        dp->setFill (Colours::transparentBlack);

        path.applyTransform (transform);
        dp->setPath (path);

        dp->setFill (getPathFillType (path, xml, "fill",
                                      getStyleAttribute (xml, "fill-opacity"),
                                      getStyleAttribute (xml, "opacity"),
                                      pathContainsClosedSubPath (path) ? Colours::black
                                                                       : Colours::transparentBlack));

        auto strokeType = getStyleAttribute (xml, "stroke");

        if (strokeType.isNotEmpty() && ! strokeType.equalsIgnoreCase ("none"))
        {
            dp->setStrokeFill (getPathFillType (path, xml, "stroke",
                                                getStyleAttribute (xml, "stroke-opacity"),
                                                getStyleAttribute (xml, "opacity"),
                                                Colours::transparentBlack));

            dp->setStrokeType (getStrokeFor (xml));
        }

        auto strokeDashArray = getStyleAttribute (xml, "stroke-dasharray");

        if (strokeDashArray.isNotEmpty())
            parseDashArray (strokeDashArray, *dp);

        parseClipPath (xml, *dp);
        return dp;
    }

    static bool pathContainsClosedSubPath (const Path& path) noexcept
    {
        for (Path::Iterator iter (path); iter.next();)
            if (iter.elementType == Path::Iterator::closePath)
                return true;

        return false;
    }

    void parseDashArray (const String& dashList, DrawablePath& dp) const
    {
        if (dashList.equalsIgnoreCase ("null") || dashList.equalsIgnoreCase ("none"))
            return;

        Array<float> dashLengths;

        for (auto t = dashList.getCharPointer();;)
        {
            float value;
            if (! parseCoord (t, value, true, true))
                break;

            dashLengths.add (value);

            t = t.findEndOfWhitespace();

            if (*t == ',')
                ++t;
        }

        if (dashLengths.size() > 0)
        {
            auto* dashes = dashLengths.getRawDataPointer();

            for (int i = 0; i < dashLengths.size(); ++i)
            {
                if (dashes[i] <= 0)  // SVG uses zero-length dashes to mean a dotted line
                {
                    if (dashLengths.size() == 1)
                        return;

                    const float nonZeroLength = 0.001f;
                    dashes[i] = nonZeroLength;

                    const int pairedIndex = i ^ 1;

                    if (isPositiveAndBelow (pairedIndex, dashLengths.size())
                          && dashes[pairedIndex] > nonZeroLength)
                        dashes[pairedIndex] -= nonZeroLength;
                }
            }

            dp.setDashLengths (dashLengths);
        }
    }

    void parseClipPath (const XmlPath& xml, Drawable& d) const
    {
        const String clipPath (getStyleAttribute (xml, "clip-path"));

        if (clipPath.isNotEmpty())
        {
            auto urlID = parseURL (clipPath);

            if (urlID.isNotEmpty())
            {
                GetClipPathOp op = { this, &d };
                topLevelXml.applyOperationToChildWithID (urlID, op);
            }
        }
    }

    void applyClipPath (Drawable& target, const XmlPath& xmlPath) const
    {
        if (xmlPath->hasTagNameIgnoringNamespace ("clipPath"))
        {
            // TODO: implement clipping..
            ignoreUnused (target);
        }
    }

    void addGradientStopsIn (ColourGradient& cg, const XmlPath& fillXml) const
    {
        if (fillXml.xml != nullptr)
        {
            forEachXmlChildElementWithTagName (*fillXml, e, "stop")
            {
                auto col = parseColour (fillXml.getChild (e), "stop-color", Colours::black);

                auto opacity = getStyleAttribute (fillXml.getChild (e), "stop-opacity", "1");
                col = col.withMultipliedAlpha (jlimit (0.0f, 1.0f, opacity.getFloatValue()));

                double offset = e->getDoubleAttribute ("offset");

                if (e->getStringAttribute ("offset").containsChar ('%'))
                    offset *= 0.01;

                cg.addColour (jlimit (0.0, 1.0, offset), col);
            }
        }
    }

    FillType getGradientFillType (const XmlPath& fillXml,
                                  const Path& path,
                                  const float opacity) const
    {
        ColourGradient gradient;

        {
            auto link = fillXml->getStringAttribute ("xlink:href");

            if (link.startsWithChar ('#'))
            {
                SetGradientStopsOp op = { this, &gradient, };
                topLevelXml.applyOperationToChildWithID (link.substring (1), op);
            }
        }

        addGradientStopsIn (gradient, fillXml);

        if (int numColours = gradient.getNumColours())
        {
            if (gradient.getColourPosition (0) > 0)
                gradient.addColour (0.0, gradient.getColour (0));

            if (gradient.getColourPosition (numColours - 1) < 1.0)
                gradient.addColour (1.0, gradient.getColour (numColours - 1));
        }
        else
        {
            gradient.addColour (0.0, Colours::black);
            gradient.addColour (1.0, Colours::black);
        }

        if (opacity < 1.0f)
            gradient.multiplyOpacity (opacity);

        jassert (gradient.getNumColours() > 0);

        gradient.isRadial = fillXml->hasTagNameIgnoringNamespace ("radialGradient");

        float gradientWidth = viewBoxW;
        float gradientHeight = viewBoxH;
        float dx = 0.0f;
        float dy = 0.0f;

        const bool userSpace = fillXml->getStringAttribute ("gradientUnits").equalsIgnoreCase ("userSpaceOnUse");

        if (! userSpace)
        {
            auto bounds = path.getBounds();
            dx = bounds.getX();
            dy = bounds.getY();
            gradientWidth = bounds.getWidth();
            gradientHeight = bounds.getHeight();
        }

        if (gradient.isRadial)
        {
            if (userSpace)
                gradient.point1.setXY (dx + getCoordLength (fillXml->getStringAttribute ("cx", "50%"), gradientWidth),
                                       dy + getCoordLength (fillXml->getStringAttribute ("cy", "50%"), gradientHeight));
            else
                gradient.point1.setXY (dx + gradientWidth  * getCoordLength (fillXml->getStringAttribute ("cx", "50%"), 1.0f),
                                       dy + gradientHeight * getCoordLength (fillXml->getStringAttribute ("cy", "50%"), 1.0f));

            auto radius = getCoordLength (fillXml->getStringAttribute ("r", "50%"), gradientWidth);
            gradient.point2 = gradient.point1 + Point<float> (radius, 0.0f);

            //xxx (the fx, fy focal point isn't handled properly here..)
        }
        else
        {
            if (userSpace)
            {
                gradient.point1.setXY (dx + getCoordLength (fillXml->getStringAttribute ("x1", "0%"), gradientWidth),
                                       dy + getCoordLength (fillXml->getStringAttribute ("y1", "0%"), gradientHeight));

                gradient.point2.setXY (dx + getCoordLength (fillXml->getStringAttribute ("x2", "100%"), gradientWidth),
                                       dy + getCoordLength (fillXml->getStringAttribute ("y2", "0%"), gradientHeight));
            }
            else
            {
                gradient.point1.setXY (dx + gradientWidth  * getCoordLength (fillXml->getStringAttribute ("x1", "0%"), 1.0f),
                                       dy + gradientHeight * getCoordLength (fillXml->getStringAttribute ("y1", "0%"), 1.0f));

                gradient.point2.setXY (dx + gradientWidth  * getCoordLength (fillXml->getStringAttribute ("x2", "100%"), 1.0f),
                                       dy + gradientHeight * getCoordLength (fillXml->getStringAttribute ("y2", "0%"), 1.0f));
            }

            if (gradient.point1 == gradient.point2)
                return Colour (gradient.getColour (gradient.getNumColours() - 1));
        }

        FillType type (gradient);

        auto gradientTransform = parseTransform (fillXml->getStringAttribute ("gradientTransform"))
                                   .followedBy (transform);

        if (gradient.isRadial)
        {
            type.transform = gradientTransform;
        }
        else
        {
            // Transform the perpendicular vector into the new coordinate space for the gradient.
            // This vector is now the slope of the linear gradient as it should appear in the new coord space
            auto perpendicular = Point<float> (gradient.point2.y - gradient.point1.y,
                                               gradient.point1.x - gradient.point2.x)
                                    .transformedBy (gradientTransform.withAbsoluteTranslation (0, 0));

            auto newGradPoint1 = gradient.point1.transformedBy (gradientTransform);
            auto newGradPoint2 = gradient.point2.transformedBy (gradientTransform);

            // Project the transformed gradient vector onto the transformed slope of the linear
            // gradient as it should appear in the new coordinate space
            const float scale = perpendicular.getDotProduct (newGradPoint2 - newGradPoint1)
                                  / perpendicular.getDotProduct (perpendicular);

            type.gradient->point1 = newGradPoint1;
            type.gradient->point2 = newGradPoint2 - perpendicular * scale;
        }

        return type;
    }

    FillType getPathFillType (const Path& path,
                              const XmlPath& xml,
                              StringRef fillAttribute,
                              const String& fillOpacity,
                              const String& overallOpacity,
                              const Colour defaultColour) const
    {
        float opacity = 1.0f;

        if (overallOpacity.isNotEmpty())
            opacity = jlimit (0.0f, 1.0f, overallOpacity.getFloatValue());

        if (fillOpacity.isNotEmpty())
            opacity *= (jlimit (0.0f, 1.0f, fillOpacity.getFloatValue()));

        String fill (getStyleAttribute (xml, fillAttribute));
        String urlID = parseURL (fill);

        if (urlID.isNotEmpty())
        {
            GetFillTypeOp op = { this, &path, opacity, FillType() };

            if (topLevelXml.applyOperationToChildWithID (urlID, op))
                return op.fillType;
        }

        if (fill.equalsIgnoreCase ("none"))
            return Colours::transparentBlack;

        return parseColour (xml, fillAttribute, defaultColour).withMultipliedAlpha (opacity);
    }

    static PathStrokeType::JointStyle getJointStyle (const String& join) noexcept
    {
        if (join.equalsIgnoreCase ("round"))  return PathStrokeType::curved;
        if (join.equalsIgnoreCase ("bevel"))  return PathStrokeType::beveled;

        return PathStrokeType::mitered;
    }

    static PathStrokeType::EndCapStyle getEndCapStyle (const String& cap) noexcept
    {
        if (cap.equalsIgnoreCase ("round"))   return PathStrokeType::rounded;
        if (cap.equalsIgnoreCase ("square"))  return PathStrokeType::square;

        return PathStrokeType::butt;
    }

    float getStrokeWidth (const String& strokeWidth) const noexcept
    {
        return transform.getScaleFactor() * getCoordLength (strokeWidth, viewBoxW);
    }

    PathStrokeType getStrokeFor (const XmlPath& xml) const
    {
        return PathStrokeType (getStrokeWidth (getStyleAttribute (xml, "stroke-width", "1")),
                               getJointStyle  (getStyleAttribute (xml, "stroke-linejoin")),
                               getEndCapStyle (getStyleAttribute (xml, "stroke-linecap")));
    }

    //==============================================================================
    Drawable* parseText (const XmlPath& xml, bool shouldParseTransform)
    {
        if (shouldParseTransform && xml->hasAttribute ("transform"))
        {
            SVGState newState (*this);
            newState.addTransform (xml);

            return newState.parseText (xml, false);
        }

        Array<float> xCoords, yCoords, dxCoords, dyCoords;

        getCoordList (xCoords,  getInheritedAttribute (xml, "x"),  true, true);
        getCoordList (yCoords,  getInheritedAttribute (xml, "y"),  true, false);
        getCoordList (dxCoords, getInheritedAttribute (xml, "dx"), true, true);
        getCoordList (dyCoords, getInheritedAttribute (xml, "dy"), true, false);

        auto font = getFont (xml);
        auto anchorStr = getStyleAttribute(xml, "text-anchor");

        auto dc = new DrawableComposite();
        setCommonAttributes (*dc, xml);

        forEachXmlChildElement (*xml, e)
        {
            if (e->isTextElement())
            {
                auto text = e->getText().trim();

                auto dt = new DrawableText();
                dc->addAndMakeVisible (dt);

                dt->setText (text);
                dt->setFont (font, true);
                dt->setTransform (transform);

                dt->setColour (parseColour (xml, "fill", Colours::black)
                                 .withMultipliedAlpha (getStyleAttribute (xml, "fill-opacity", "1").getFloatValue()));

                Rectangle<float> bounds (xCoords[0], yCoords[0] - font.getAscent(),
                                         font.getStringWidthFloat (text), font.getHeight());

                if (anchorStr == "middle")   bounds.setX (bounds.getX() - bounds.getWidth() / 2.0f);
                else if (anchorStr == "end") bounds.setX (bounds.getX() - bounds.getWidth());

                dt->setBoundingBox (bounds);
            }
            else if (e->hasTagNameIgnoringNamespace ("tspan"))
            {
                dc->addAndMakeVisible (parseText (xml.getChild (e), true));
            }
        }

        return dc;
    }

    Font getFont (const XmlPath& xml) const
    {
        auto fontSize = getCoordLength (getStyleAttribute (xml, "font-size"), 1.0f);

        int style = getStyleAttribute (xml, "font-style").containsIgnoreCase ("italic") ? Font::italic : Font::plain;

        if (getStyleAttribute (xml, "font-weight").containsIgnoreCase ("bold"))
            style |= Font::bold;

        auto family = getStyleAttribute (xml, "font-family");

        return family.isEmpty() ? Font (fontSize, style)
                                : Font (family, fontSize, style);
    }

    //==============================================================================
    void addTransform (const XmlPath& xml)
    {
        transform = parseTransform (xml->getStringAttribute ("transform"))
                        .followedBy (transform);
    }

    //==============================================================================
    bool parseCoord (String::CharPointerType& s, float& value, const bool allowUnits, const bool isX) const
    {
        String number;

        if (! parseNextNumber (s, number, allowUnits))
        {
            value = 0;
            return false;
        }

        value = getCoordLength (number, isX ? viewBoxW : viewBoxH);
        return true;
    }

    bool parseCoords (String::CharPointerType& s, Point<float>& p, const bool allowUnits) const
    {
        return parseCoord (s, p.x, allowUnits, true)
            && parseCoord (s, p.y, allowUnits, false);
    }

    bool parseCoordsOrSkip (String::CharPointerType& s, Point<float>& p, const bool allowUnits) const
    {
        if (parseCoords (s, p, allowUnits))
            return true;

        if (! s.isEmpty()) ++s;
        return false;
    }

    float getCoordLength (const String& s, const float sizeForProportions) const noexcept
    {
        float n = s.getFloatValue();
        const int len = s.length();

        if (len > 2)
        {
            const float dpi = 96.0f;

            const juce_wchar n1 = s [len - 2];
            const juce_wchar n2 = s [len - 1];

            if (n1 == 'i' && n2 == 'n')         n *= dpi;
            else if (n1 == 'm' && n2 == 'm')    n *= dpi / 25.4f;
            else if (n1 == 'c' && n2 == 'm')    n *= dpi / 2.54f;
            else if (n1 == 'p' && n2 == 'c')    n *= 15.0f;
            else if (n2 == '%')                 n *= 0.01f * sizeForProportions;
        }

        return n;
    }

    float getCoordLength (const XmlPath& xml, const char* attName, const float sizeForProportions) const noexcept
    {
        return getCoordLength (xml->getStringAttribute (attName), sizeForProportions);
    }

    void getCoordList (Array<float>& coords, const String& list, bool allowUnits, const bool isX) const
    {
        auto text = list.getCharPointer();
        float value;

        while (parseCoord (text, value, allowUnits, isX))
            coords.add (value);
    }

    //==============================================================================
    void parseCSSStyle (const XmlPath& xml)
    {
        cssStyleText = xml->getAllSubText() + "\n" + cssStyleText;
    }

    void parseDefs (const XmlPath& xml)
    {
        if (auto* style = xml->getChildByName ("style"))
            parseCSSStyle (xml.getChild (style));
    }

    static String::CharPointerType findStyleItem (String::CharPointerType source, String::CharPointerType name)
    {
        auto nameLength = (int) name.length();

        while (! source.isEmpty())
        {
            if (source.getAndAdvance() == '.'
                 && CharacterFunctions::compareIgnoreCaseUpTo (source, name, nameLength) == 0)
            {
                auto endOfName = (source + nameLength).findEndOfWhitespace();

                if (*endOfName == '{')
                    return endOfName;

                if (*endOfName == ',')
                    return CharacterFunctions::find (endOfName, (juce_wchar) '{');
            }
        }

        return source;
    }

    String getStyleAttribute (const XmlPath& xml, StringRef attributeName, const String& defaultValue = String()) const
    {
        if (xml->hasAttribute (attributeName))
            return xml->getStringAttribute (attributeName, defaultValue);

        auto styleAtt = xml->getStringAttribute ("style");

        if (styleAtt.isNotEmpty())
        {
            auto value = getAttributeFromStyleList (styleAtt, attributeName, {});

            if (value.isNotEmpty())
                return value;
        }
        else if (xml->hasAttribute ("class"))
        {
            for (auto i = cssStyleText.getCharPointer();;)
            {
                auto openBrace = findStyleItem (i, xml->getStringAttribute ("class").getCharPointer());

                if (openBrace.isEmpty())
                    break;

                auto closeBrace = CharacterFunctions::find (openBrace, (juce_wchar) '}');

                if (closeBrace.isEmpty())
                    break;

                auto value = getAttributeFromStyleList (String (openBrace + 1, closeBrace),
                                                        attributeName, defaultValue);
                if (value.isNotEmpty())
                    return value;

                i = closeBrace + 1;
            }
        }

        if (xml.parent != nullptr)
            return getStyleAttribute (*xml.parent, attributeName, defaultValue);

        return defaultValue;
    }

    String getInheritedAttribute (const XmlPath& xml, StringRef attributeName) const
    {
        if (xml->hasAttribute (attributeName))
            return xml->getStringAttribute (attributeName);

        if (xml.parent != nullptr)
            return getInheritedAttribute (*xml.parent, attributeName);

        return {};
    }

    static int parsePlacementFlags (const String& align) noexcept
    {
        if (align.isEmpty())
            return 0;

        if (align.containsIgnoreCase ("none"))
            return RectanglePlacement::stretchToFit;

        return (align.containsIgnoreCase ("slice") ? RectanglePlacement::fillDestination : 0)
             | (align.containsIgnoreCase ("xMin")  ? RectanglePlacement::xLeft
                                                   : (align.containsIgnoreCase ("xMax") ? RectanglePlacement::xRight
                                                                                        : RectanglePlacement::xMid))
             | (align.containsIgnoreCase ("yMin")  ? RectanglePlacement::yTop
                                                   : (align.containsIgnoreCase ("yMax") ? RectanglePlacement::yBottom
                                                                                        : RectanglePlacement::yMid));
    }

    //==============================================================================
    static bool isIdentifierChar (const juce_wchar c)
    {
        return CharacterFunctions::isLetter (c) || c == '-';
    }

    static String getAttributeFromStyleList (const String& list, StringRef attributeName, const String& defaultValue)
    {
        int i = 0;

        for (;;)
        {
            i = list.indexOf (i, attributeName);

            if (i < 0)
                break;

            if ((i == 0 || (i > 0 && ! isIdentifierChar (list [i - 1])))
                 && ! isIdentifierChar (list [i + attributeName.length()]))
            {
                i = list.indexOfChar (i, ':');

                if (i < 0)
                    break;

                int end = list.indexOfChar (i, ';');

                if (end < 0)
                    end = 0x7ffff;

                return list.substring (i + 1, end).trim();
            }

            ++i;
        }

        return defaultValue;
    }

    //==============================================================================
    static bool isStartOfNumber (juce_wchar c) noexcept
    {
        return CharacterFunctions::isDigit (c) || c == '-' || c == '+';
    }

    static bool parseNextNumber (String::CharPointerType& text, String& value, const bool allowUnits)
    {
        String::CharPointerType s (text);

        while (s.isWhitespace() || *s == ',')
            ++s;

        auto start = s;

        if (isStartOfNumber (*s))
            ++s;

        while (s.isDigit())
            ++s;

        if (*s == '.')
        {
            ++s;

            while (s.isDigit())
                ++s;
        }

        if ((*s == 'e' || *s == 'E') && isStartOfNumber (s[1]))
        {
            s += 2;

            while (s.isDigit())
                ++s;
        }

        if (allowUnits)
            while (s.isLetter())
                ++s;

        if (s == start)
        {
            text = s;
            return false;
        }

        value = String (start, s);

        while (s.isWhitespace() || *s == ',')
            ++s;

        text = s;
        return true;
    }

    //==============================================================================
    Colour parseColour (const XmlPath& xml, StringRef attributeName, const Colour defaultColour) const
    {
        auto text = getStyleAttribute (xml, attributeName);

        if (text.startsWithChar ('#'))
        {
            uint32 hex[6] = { 0 };
            int numChars = 0;
            auto s = text.getCharPointer();

            while (numChars < 6)
            {
                auto hexValue = CharacterFunctions::getHexDigitValue (*++s);

                if (hexValue >= 0)
                    hex [numChars++] = (uint32) hexValue;
                else
                    break;
            }

            if (numChars <= 3)
                return Colour ((uint8) (hex[0] * 0x11),
                               (uint8) (hex[1] * 0x11),
                               (uint8) (hex[2] * 0x11));

            return Colour ((uint8) ((hex[0] << 4) + hex[1]),
                           (uint8) ((hex[2] << 4) + hex[3]),
                           (uint8) ((hex[4] << 4) + hex[5]));
        }

        if (text.startsWith ("rgb"))
        {
            auto openBracket = text.indexOfChar ('(');
            auto closeBracket = text.indexOfChar (openBracket, ')');

            if (openBracket >= 3 && closeBracket > openBracket)
            {
                StringArray tokens;
                tokens.addTokens (text.substring (openBracket + 1, closeBracket), ",", "");
                tokens.trim();
                tokens.removeEmptyStrings();

                if (tokens[0].containsChar ('%'))
                    return Colour ((uint8) roundToInt (2.55 * tokens[0].getDoubleValue()),
                                   (uint8) roundToInt (2.55 * tokens[1].getDoubleValue()),
                                   (uint8) roundToInt (2.55 * tokens[2].getDoubleValue()));

                return Colour ((uint8) tokens[0].getIntValue(),
                               (uint8) tokens[1].getIntValue(),
                               (uint8) tokens[2].getIntValue());
            }
        }

        if (text == "inherit")
        {
            for (const XmlPath* p = xml.parent; p != nullptr; p = p->parent)
                if (getStyleAttribute (*p, attributeName).isNotEmpty())
                    return parseColour (*p, attributeName, defaultColour);
        }

        return Colours::findColourForName (text, defaultColour);
    }

    static AffineTransform parseTransform (String t)
    {
        AffineTransform result;

        while (t.isNotEmpty())
        {
            StringArray tokens;
            tokens.addTokens (t.fromFirstOccurrenceOf ("(", false, false)
                               .upToFirstOccurrenceOf (")", false, false),
                              ", ", "");

            tokens.removeEmptyStrings (true);

            float numbers[6];

            for (int i = 0; i < numElementsInArray (numbers); ++i)
                numbers[i] = tokens[i].getFloatValue();

            AffineTransform trans;

            if (t.startsWithIgnoreCase ("matrix"))
            {
                trans = AffineTransform (numbers[0], numbers[2], numbers[4],
                                         numbers[1], numbers[3], numbers[5]);
            }
            else if (t.startsWithIgnoreCase ("translate"))
            {
                trans = AffineTransform::translation (numbers[0], numbers[1]);
            }
            else if (t.startsWithIgnoreCase ("scale"))
            {
                trans = AffineTransform::scale (numbers[0], numbers[tokens.size() > 1 ? 1 : 0]);
            }
            else if (t.startsWithIgnoreCase ("rotate"))
            {
                trans = AffineTransform::rotation (degreesToRadians (numbers[0]), numbers[1], numbers[2]);
            }
            else if (t.startsWithIgnoreCase ("skewX"))
            {
                trans = AffineTransform::shear (std::tan (degreesToRadians (numbers[0])), 0.0f);
            }
            else if (t.startsWithIgnoreCase ("skewY"))
            {
                trans = AffineTransform::shear (0.0f, std::tan (degreesToRadians (numbers[0])));
            }

            result = trans.followedBy (result);
            t = t.fromFirstOccurrenceOf (")", false, false).trimStart();
        }

        return result;
    }

    static void endpointToCentreParameters (const double x1, const double y1,
                                            const double x2, const double y2,
                                            const double angle,
                                            const bool largeArc, const bool sweep,
                                            double& rx, double& ry,
                                            double& centreX, double& centreY,
                                            double& startAngle, double& deltaAngle) noexcept
    {
        const double midX = (x1 - x2) * 0.5;
        const double midY = (y1 - y2) * 0.5;

        const double cosAngle = std::cos (angle);
        const double sinAngle = std::sin (angle);
        const double xp = cosAngle * midX + sinAngle * midY;
        const double yp = cosAngle * midY - sinAngle * midX;
        const double xp2 = xp * xp;
        const double yp2 = yp * yp;

        double rx2 = rx * rx;
        double ry2 = ry * ry;

        const double s = (xp2 / rx2) + (yp2 / ry2);
        double c;

        if (s <= 1.0)
        {
            c = std::sqrt (jmax (0.0, ((rx2 * ry2) - (rx2 * yp2) - (ry2 * xp2))
                                         / (( rx2 * yp2) + (ry2 * xp2))));

            if (largeArc == sweep)
                c = -c;
        }
        else
        {
            const double s2 = std::sqrt (s);
            rx *= s2;
            ry *= s2;
            c = 0;
        }

        const double cpx = ((rx * yp) / ry) * c;
        const double cpy = ((-ry * xp) / rx) * c;

        centreX = ((x1 + x2) * 0.5) + (cosAngle * cpx) - (sinAngle * cpy);
        centreY = ((y1 + y2) * 0.5) + (sinAngle * cpx) + (cosAngle * cpy);

        const double ux = (xp - cpx) / rx;
        const double uy = (yp - cpy) / ry;
        const double vx = (-xp - cpx) / rx;
        const double vy = (-yp - cpy) / ry;

        const double length = juce_hypot (ux, uy);

        startAngle = acos (jlimit (-1.0, 1.0, ux / length));

        if (uy < 0)
            startAngle = -startAngle;

        startAngle += double_Pi * 0.5;

        deltaAngle = acos (jlimit (-1.0, 1.0, ((ux * vx) + (uy * vy))
                                                / (length * juce_hypot (vx, vy))));

        if ((ux * vy) - (uy * vx) < 0)
            deltaAngle = -deltaAngle;

        if (sweep)
        {
            if (deltaAngle < 0)
                deltaAngle += double_Pi * 2.0;
        }
        else
        {
            if (deltaAngle > 0)
                deltaAngle -= double_Pi * 2.0;
        }

        deltaAngle = fmod (deltaAngle, double_Pi * 2.0);
    }

    SVGState& operator= (const SVGState&) JUCE_DELETED_FUNCTION;
};


//==============================================================================
Drawable* Drawable::createFromSVG (const XmlElement& svgDocument)
{
    if (! svgDocument.hasTagNameIgnoringNamespace ("svg"))
        return nullptr;

    SVGState state (&svgDocument);
    return state.parseSVGElement (SVGState::XmlPath (&svgDocument, nullptr));
}

Path Drawable::parseSVGPath (const String& svgPath)
{
    SVGState state (nullptr);
    Path p;
    state.parsePathString (p, svgPath);
    return p;
}
