// SPDX-License-Identifier: Apache-2.0
#include "StdAfx.h"

#include <SmApiBrep.h>
#include <SmApiGeneral.h>
#include <SmApiPrimitives.h>
#include <SmApiQueries.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
using Owner = std::unique_ptr<SmBrep>;
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
void Check(SmApiStatus status)
{
    Require(status == SM_SUCCESS, "SM_API operation failed");
}
struct Snapshot
{
    long solids, voids;
    double volume;
    SmPoint3d minimum, maximum;
    ULONG faces;
};
Snapshot Inspect(const SmBrep* brep)
{
    Require(brep && brep->AssertValid(nullptr, SM_LEVEL_2), "Level-2 validation failed");
    SmBoolean manifold = FALSE;
    Check(SmApiBrepIsManifoldSolid(brep, manifold));
    Require(manifold == TRUE, "Not a manifold solid");
    Snapshot s{};
    Check(SmApiBrepMaterialCensus(brep, s.solids, s.voids));
    Check(SmApiBrepComputeVolume(brep, 1.0e-6, s.volume));
    Check(SmApiBrepBoundingBox(brep, TRUE, s.minimum, s.maximum));
    s.faces = brep->GetNumFaces();
    return s;
}
void Unchanged(const Snapshot& a, const Snapshot& b)
{
    Require(a.solids == b.solids && a.voids == b.voids && a.faces == b.faces, "Retained original classification changed");
    Require(std::abs(a.volume - b.volume) < 1.0e-6, "Retained original volume changed");
    for (int i = 0; i < 3; ++i)
    {
        Require(std::abs(a.minimum[i] - b.minimum[i]) < 1.0e-8 && std::abs(a.maximum[i] - b.maximum[i]) < 1.0e-8, "Retained original bounds changed");
    }
}
Owner Box(double x, double y, double z, double size)
{
    SmBrep* b = nullptr;
    Check(SmApiCreateBox(SmVector3d(x, y, z), size, size, size, b));
    Require(b != nullptr, "Missing box");
    return Owner(b);
}
Owner Combine(const SmBrep* a, const SmBrep* b, SmBooleanOperationType operation)
{
    const Snapshot beforeA = Inspect(a), beforeB = Inspect(b);
    SmBrep *copyA = nullptr, *copyB = nullptr;
    Check(SmApiBrepCopy(a, copyA));
    Owner ownedA(copyA);
    Check(SmApiBrepCopy(b, copyB));
    Owner ownedB(copyB);
    Unchanged(beforeA, Inspect(copyA));
    Unchanged(beforeB, Inspect(copyB));
    // Success mutates A and deletes B. Only deep copies are consumed; never
    // inspect consumed handles or claim the API preserves its operands.
    SmBrep* result = nullptr;
    const SmApiStatus status = SmApiBoolean(copyA, copyB, operation, result);
    if (status == SM_SUCCESS)
    {
        ownedB.release();
    }
    Check(status);
    Require(result == ownedA.get(), "Unexpected result ownership");
    Unchanged(beforeA, Inspect(a));
    Unchanged(beforeB, Inspect(b));
    return ownedA;
}
void Expected(const SmBrep* b, double volume, long solids, long voids, double maxX)
{
    const Snapshot s = Inspect(b);
    Require(s.solids == solids && s.voids == voids, "Wrong material/void census");
    Require(std::isfinite(s.volume) && std::abs(s.volume - volume) <= 1.0e-6 * volume, "Wrong analytic volume");
    const double max[] = { maxX, 10, 10 };
    for (int i = 0; i < 3; ++i)
    {
        Require(
            std::isfinite(s.minimum[i]) && std::isfinite(s.maximum[i]) && std::abs(s.minimum[i]) < 1.0e-4 && std::abs(s.maximum[i] - max[i]) < 1.0e-4,
            "Wrong tight bounds"
        );
    }
}
void BothOrders(const SmBrep* a, const SmBrep* b, double volume, long solids, double maxX)
{
    for (int reverse = 0; reverse < 2; ++reverse)
    {
        Owner result = reverse ? Combine(b, a, SM_BO_UNION) : Combine(a, b, SM_BO_UNION);
        Expected(result.get(), volume, solids, 0, maxX);
    }
}
} // namespace

SmStatus TestSmUnionMaterialSurvivor()
{
    const char* currentCase = "construction";
    try
    {
        SmApiCreateContext();
        Owner outer = Box(0, 0, 0, 10), inner = Box(2, 2, 2, 6), far = Box(20, 0, 0, 10);
        currentCase = "ordinary overlap";
        Owner overlap = Box(5, 0, 0, 10);
        BothOrders(outer.get(), overlap.get(), 1500, 1, 15);
        currentCase = "disjoint union";
        BothOrders(outer.get(), far.get(), 2000, 2, 30);
        currentCase = "cavity difference";
        Owner hollow = Combine(outer.get(), inner.get(), SM_BO_DIFFERENCE);
        Expected(hollow.get(), 784, 1, 1, 10);
        // An internal face deletion can join an unattributed solid to a void;
        // UNION must retain that existing material without reclassifying it.
        currentCase = "exact cavity fill";
        BothOrders(hollow.get(), inner.get(), 1000, 1, 10);
        currentCase = "enclosing cavity fill";
        Owner enclosing = Box(1, 1, 1, 8);
        BothOrders(hollow.get(), enclosing.get(), 1000, 1, 10);
        currentCase = "cavity fill with disjoint solid";
        Owner composite = Combine(hollow.get(), far.get(), SM_BO_UNION);
        Expected(composite.get(), 1784, 2, 1, 30);
        BothOrders(composite.get(), inner.get(), 2000, 2, 30);
        return SM_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Union material survivor (" << currentCase << "): " << error.what() << '\n';
        return SM_ERR;
    }
}
