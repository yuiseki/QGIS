# The following has been generated automatically from src/core/geometry/qgsellipse.h
try:
    QgsEllipse.fromFoci = staticmethod(QgsEllipse.fromFoci)
    QgsEllipse.fromExtent = staticmethod(QgsEllipse.fromExtent)
    QgsEllipse.fromCenterPoint = staticmethod(QgsEllipse.fromCenterPoint)
    QgsEllipse.fromCenter2Points = staticmethod(QgsEllipse.fromCenter2Points)
    QgsEllipse.__virtual_methods__ = ['isEmpty', 'setSemiMajorAxis', 'setSemiMinorAxis', 'focusDistance', 'foci', 'eccentricity', 'area', 'perimeter', 'quadrant', 'points', 'toPolygon', 'toLineString', 'orientedBoundingBox', 'boundingBox', 'toString']
    QgsEllipse.__group__ = ['geometry']
except (NameError, AttributeError):
    pass
