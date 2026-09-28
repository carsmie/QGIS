// Verifies OGRLayer spatial filtering against brute-force GEOS Intersects(),
// and benchmarks tile-sized spatial filter queries.
//
// usage: harness verify <dataset>... | harness synth | harness bench <dataset> <n>
#include "gdal_priv.h"
#include "ogrsf_frmts.h"
#include "ogr_geometry.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
#include <set>
#include <vector>

static std::mt19937_64 rng(42);
static std::set<GIntBig> gInvalid;

static void Dump(const OGREnvelope &e, const std::set<GIntBig> &got)
{
    if (!getenv("HARNESS_DUMP"))
        return;
    printf("R %.17g %.17g %.17g %.17g:", e.MinX, e.MinY, e.MaxX, e.MaxY);
    for (auto fid : got)
        printf(" %lld", (long long)fid);
    printf("\n");
}


static double U(double a, double b)
{
    return std::uniform_real_distribution<double>(a, b)(rng);
}

struct Feat
{
    GIntBig fid;
    std::unique_ptr<OGRGeometry> geom;
};

static std::vector<Feat> LoadAll(OGRLayer *poLayer)
{
    std::vector<Feat> v;
    poLayer->SetSpatialFilter(nullptr);
    poLayer->ResetReading();
    for (auto &&f : *poLayer)
    {
        std::unique_ptr<OGRGeometry> g(f->StealGeometry());
        if (g && !g->IsEmpty() && !g->IsValid())
        {
            gInvalid.insert(f->GetFID());
            if (!getenv("HARNESS_DUMP"))
                continue;
        }
        v.push_back({f->GetFID(), std::move(g)});
    }
    return v;
}

// Rectangles: random sizes, plus rectangles whose edges go exactly through
// vertices of the data, to exercise boundary cases.
static std::vector<OGREnvelope> MakeRects(const std::vector<Feat> &feats,
                                          const OGREnvelope &ext, int n)
{
    std::vector<double> xs, ys;
    // collect vertices
    for (const auto &f : feats)
    {
        if (!f.geom)
            continue;
        OGRGeometry *g = f.geom.get();
        std::vector<const OGRGeometry *> stack{g};
        while (!stack.empty())
        {
            auto cur = stack.back();
            stack.pop_back();
            auto t = wkbFlatten(cur->getGeometryType());
            if (t == wkbPoint)
            {
                if (!cur->IsEmpty())
                {
                    xs.push_back(cur->toPoint()->getX());
                    ys.push_back(cur->toPoint()->getY());
                }
            }
            else if (t == wkbLineString || t == wkbLinearRing)
            {
                auto ls = cur->toSimpleCurve();
                for (int i = 0; i < ls->getNumPoints(); ++i)
                {
                    xs.push_back(ls->getX(i));
                    ys.push_back(ls->getY(i));
                }
            }
            else if (t == wkbPolygon)
            {
                for (auto r : *cur->toPolygon())
                    stack.push_back(r);
            }
            else if (OGR_GT_IsSubClassOf(t, wkbGeometryCollection))
            {
                for (auto s : *cur->toGeometryCollection())
                    stack.push_back(s);
            }
        }
    }
    std::vector<OGREnvelope> rects;
    const double W = ext.MaxX - ext.MinX, H = ext.MaxY - ext.MinY;
    for (int i = 0; i < n; ++i)
    {
        const double s = std::max(W, H) * std::pow(10.0, U(-5, -0.3));
        const double w = s * U(0.2, 1.0), h = s * U(0.2, 1.0);
        OGREnvelope e;
        const int mode = i % 4;
        if (mode == 0 || xs.empty())
        {
            e.MinX = U(ext.MinX - w, ext.MaxX);
            e.MinY = U(ext.MinY - h, ext.MaxY);
        }
        else
        {
            size_t k = rng() % xs.size();
            // mode 1: rect edge exactly on a vertex x; 2: corner on vertex;
            // 3: rect just around a vertex neighbourhood
            if (mode == 1)
            {
                e.MinX = xs[k];
                e.MinY = ys[k] - U(0, h);
            }
            else if (mode == 2)
            {
                e.MinX = xs[k] - w;
                e.MinY = ys[k];
            }
            else
            {
                e.MinX = xs[k] + U(-2 * w, w);
                e.MinY = ys[k] + U(-2 * h, h);
            }
        }
        e.MaxX = e.MinX + w;
        e.MaxY = e.MinY + h;
        if (mode == 2)
        {
            // make MaxX exactly on the vertex too
            e.MaxX = e.MinX + w;
        }
        rects.push_back(e);
    }
    return rects;
}

static int Verify(OGRLayer *poLayer, int nRects, const char *pszName)
{
    auto feats = LoadAll(poLayer);
    OGREnvelope ext;
    poLayer->GetExtent(&ext, true);
    auto rects = MakeRects(feats, ext, nRects);
    long nMismatch = 0, nChecks = 0, nTrue = 0;
    for (const auto &e : rects)
    {
        OGRPolygon oRect(e);
        std::set<GIntBig> expected, got;
        for (const auto &f : feats)
        {
            if (f.geom && !f.geom->IsEmpty() && oRect.Intersects(f.geom.get()))
                expected.insert(f.fid);
        }
        poLayer->SetSpatialFilterRect(e.MinX, e.MinY, e.MaxX, e.MaxY);
        poLayer->ResetReading();
        for (auto &&f : *poLayer)
            got.insert(f->GetFID());
        Dump(e, got);
        for (auto fid : gInvalid)
            got.erase(fid);
        nChecks += feats.size();
        nTrue += expected.size();
        if (expected != got)
        {
            for (auto fid : expected)
                if (!got.count(fid))
                {
                    ++nMismatch;
                    printf("  MISSING fid=%lld rect=%.17g %.17g %.17g %.17g\n",
                           (long long)fid, e.MinX, e.MinY, e.MaxX, e.MaxY);
                }
            for (auto fid : got)
                if (!expected.count(fid))
                {
                    ++nMismatch;
                    printf("  EXTRA fid=%lld rect=%.17g %.17g %.17g %.17g\n",
                           (long long)fid, e.MinX, e.MinY, e.MaxX, e.MaxY);
                }
        }
    }
    printf("%s: %zu invalid geometries excluded\n", pszName, gInvalid.size());
    printf("%s: %zu rects, %ld feature checks, %ld intersecting, %ld "
           "mismatches\n",
           pszName, rects.size(), nChecks, nTrue, nMismatch);
    return nMismatch == 0 ? 0 : 1;
}

static std::unique_ptr<OGRGeometry> FromWkt(const char *wkt)
{
    OGRGeometry *g = nullptr;
    OGRGeometryFactory::createFromWkt(wkt, nullptr, &g);
    return std::unique_ptr<OGRGeometry>(g);
}

static OGRLinearRing *RandomRing(double cx, double cy, double r, int n,
                                 bool bClose, bool bReverse)
{
    auto ring = new OGRLinearRing();
    std::vector<double> angles;
    for (int i = 0; i < n; ++i)
        angles.push_back(U(0, 2 * M_PI));
    std::sort(angles.begin(), angles.end());
    if (bReverse)
        std::reverse(angles.begin(), angles.end());
    for (double a : angles)
    {
        double rr = r * U(0.5, 1.0);
        ring->addPoint(cx + rr * cos(a), cy + rr * sin(a));
    }
    if (bClose)
        ring->closeRings();
    return ring;
}

static int Synth(int nFeatures, int nRects)
{
    auto poDrv = GetGDALDriverManager()->GetDriverByName("MEM");
    std::unique_ptr<GDALDataset> poDS(
        poDrv->Create("", 0, 0, 0, GDT_Unknown, nullptr));
    auto poLayer = poDS->CreateLayer("synth", nullptr, wkbUnknown, nullptr);
    std::vector<std::unique_ptr<OGRGeometry>> geoms;
    for (int i = 0; i < nFeatures; ++i)
    {
        const double cx = U(0, 100), cy = U(0, 100), r = U(0.1, 40);
        switch (i % 9)
        {
            case 0:
            {
                auto p = std::make_unique<OGRPolygon>();
                p->addRingDirectly(RandomRing(cx, cy, r, 16 + rng() % 200,
                                              true, rng() % 2));
                p->addRingDirectly(
                    RandomRing(cx, cy, r * 0.1, 3 + rng() % 20, true, false));
                geoms.push_back(std::move(p));
                break;
            }
            case 1:
            {
                auto mp = std::make_unique<OGRMultiPolygon>();
                for (int k = 0; k < 3; ++k)
                {
                    auto p = new OGRPolygon();
                    p->addRingDirectly(RandomRing(U(0, 100), U(0, 100),
                                                  U(0.1, 10), 3 + rng() % 50,
                                                  true, false));
                    mp->addGeometryDirectly(p);
                }
                geoms.push_back(std::move(mp));
                break;
            }
            case 2:
            {
                auto ls = std::make_unique<OGRLineString>();
                const int n = 2 + rng() % 50;
                for (int k = 0; k < n; ++k)
                    ls->addPoint(U(-10, 110), U(-10, 110));
                geoms.push_back(std::move(ls));
                break;
            }
            case 3:
            {
                // axis-parallel segments on integer coordinates, to hit
                // exact touching cases with integer rectangles
                auto ls = std::make_unique<OGRLineString>();
                double x = std::floor(U(0, 100)), y = std::floor(U(0, 100));
                ls->addPoint(x, y);
                for (int k = 0; k < 6; ++k)
                {
                    if (k % 2)
                        x = std::floor(U(0, 100));
                    else
                        y = std::floor(U(0, 100));
                    ls->addPoint(x, y);
                }
                geoms.push_back(std::move(ls));
                break;
            }
            case 4:
            {
                auto mpt = std::make_unique<OGRMultiPoint>();
                for (int k = 0; k < 5; ++k)
                    mpt->addGeometryDirectly(
                        new OGRPoint(U(0, 100), U(0, 100)));
                geoms.push_back(std::move(mpt));
                break;
            }
            case 5:
            {
                auto gc = std::make_unique<OGRGeometryCollection>();
                gc->addGeometryDirectly(new OGRPoint(U(0, 100), U(0, 100)));
                auto p = new OGRPolygon();
                p->addRingDirectly(RandomRing(cx, cy, r, 20, true, false));
                gc->addGeometryDirectly(p);
                gc->addGeometryDirectly(new OGRLineString());
                geoms.push_back(std::move(gc));
                break;
            }
            case 6:
            {
                // big thin diagonal polygon: bbox covers much, few vertices
                auto p = std::make_unique<OGRPolygon>();
                auto ring = new OGRLinearRing();
                ring->addPoint(0, 0);
                ring->addPoint(100, 100 - U(0, 5));
                ring->addPoint(100, 100);
                ring->addPoint(U(0, 5), 100);
                ring->closeRings();
                p->addRingDirectly(ring);
                geoms.push_back(std::move(p));
                break;
            }
            case 7:
            {
                // integer-coordinate square polygons with holes
                double x = std::floor(U(0, 80)), y = std::floor(U(0, 80));
                double s = std::floor(U(3, 20));
                auto p = std::make_unique<OGRPolygon>();
                auto outer = new OGRLinearRing();
                outer->addPoint(x, y);
                outer->addPoint(x + s, y);
                outer->addPoint(x + s, y + s);
                outer->addPoint(x, y + s);
                outer->closeRings();
                auto hole = new OGRLinearRing();
                hole->addPoint(x + 1, y + 1);
                hole->addPoint(x + 1, y + s - 1);
                hole->addPoint(x + s - 1, y + s - 1);
                hole->addPoint(x + s - 1, y + 1);
                hole->closeRings();
                p->addRingDirectly(outer);
                p->addRingDirectly(hole);
                geoms.push_back(std::move(p));
                break;
            }
            default:
            {
                // polygon whose ring is not explicitly closed
                auto p = std::make_unique<OGRPolygon>();
                p->addRingDirectly(RandomRing(cx, cy, r, 3 + rng() % 30,
                                              false, false));
                geoms.push_back(std::move(p));
                break;
            }
        }
    }
    geoms.push_back(FromWkt("POINT EMPTY"));
    geoms.push_back(FromWkt("POLYGON EMPTY"));
    geoms.push_back(FromWkt("CIRCULARSTRING (0 0,50 50,100 0)"));
    geoms.push_back(FromWkt("CURVEPOLYGON (CIRCULARSTRING (10 10,90 10,90 "
                            "90,10 90,10 10))"));
    for (auto &g : geoms)
    {
        OGRFeature f(poLayer->GetLayerDefn());
        f.SetGeometry(g.get());
        poLayer->CreateFeature(&f);
    }
    int ret = Verify(poLayer, nRects, "synthetic (random rects)");

    // Integer rectangles, to hit exact touching with integer geometries
    {
        auto feats = LoadAll(poLayer);
        long nMismatch = 0;
        long nTrue = 0;
        for (int i = 0; i < nRects; ++i)
        {
            OGREnvelope e;
            e.MinX = std::floor(U(-5, 100));
            e.MinY = std::floor(U(-5, 100));
            e.MaxX = e.MinX + 1 + std::floor(U(0, 10));
            e.MaxY = e.MinY + 1 + std::floor(U(0, 10));
            OGRPolygon oRect(e);
            if (!oRect.IsRectangle())
                continue;
            std::set<GIntBig> expected, got;
            for (const auto &f : feats)
                if (f.geom && !f.geom->IsEmpty() &&
                    oRect.Intersects(f.geom.get()))
                    expected.insert(f.fid);
            poLayer->SetSpatialFilterRect(e.MinX, e.MinY, e.MaxX, e.MaxY);
            poLayer->ResetReading();
            for (auto &&f : *poLayer)
                got.insert(f->GetFID());
            Dump(e, got);
            for (auto fid : gInvalid)
                got.erase(fid);
            nTrue += expected.size();
            if (expected != got)
            {
                ++nMismatch;
                printf("  MISMATCH integer rect %g %g %g %g\n", e.MinX,
                       e.MinY, e.MaxX, e.MaxY);
                for (const auto &f : feats)
                {
                    const bool a = expected.count(f.fid) > 0;
                    const bool b = got.count(f.fid) > 0;
                    if (a != b)
                    {
                        char *wkt = nullptr;
                        f.geom->exportToWkt(&wkt);
                        printf("    fid=%lld valid=%d expected=%d got=%d %.200s\n",
                               (long long)f.fid, f.geom->IsValid() ? 1 : 0, a, b, wkt);
                        CPLFree(wkt);
                    }
                }
            }
        }
        printf("synthetic (integer rects): %ld intersecting, %ld mismatching "
               "rects\n",
               nTrue, nMismatch);
        if (nMismatch)
            ret = 1;
    }
    return ret;
}

// Tiles of a web-map-like pyramid over the layer extent; times total
// time to iterate features with a spatial filter.
static int Bench(OGRLayer *poLayer, int nTiles, double dfTileSize)
{
    OGREnvelope ext;
    poLayer->GetExtent(&ext, true);
    std::vector<OGREnvelope> tiles;
    std::mt19937_64 r2(7);
    std::uniform_real_distribution<double> ux(ext.MinX, ext.MaxX - dfTileSize),
        uy(std::max(ext.MinY, -60.0), std::min(ext.MaxY, 80.0) - dfTileSize);
    for (int i = 0; i < nTiles; ++i)
    {
        OGREnvelope e;
        e.MinX = ux(r2);
        e.MinY = uy(r2);
        e.MaxX = e.MinX + dfTileSize;
        e.MaxY = e.MinY + dfTileSize;
        tiles.push_back(e);
    }
    long nFeat = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (const auto &e : tiles)
    {
        poLayer->SetSpatialFilterRect(e.MinX, e.MinY, e.MaxX, e.MaxY);
        poLayer->ResetReading();
        for (auto &&f : *poLayer)
        {
            (void)f;
            ++nFeat;
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("tile %g deg: %d tiles, %ld features, %.1f ms total, %.3f ms/tile\n",
           dfTileSize, nTiles, nFeat, ms, ms / nTiles);
    return 0;
}

int main(int argc, char **argv)
{
    GDALAllRegister();
    if (argc < 2)
        return 2;
    const std::string mode = argv[1];
    if (mode == "synth")
        return Synth(argc > 2 ? atoi(argv[2]) : 3000,
                     argc > 3 ? atoi(argv[3]) : 2000);
    int ret = 0;
    if (mode == "verify")
    {
        for (int i = 2; i < argc; ++i)
        {
            std::unique_ptr<GDALDataset> poDS(
                GDALDataset::Open(argv[i], GDAL_OF_VECTOR));
            if (!poDS)
                return 1;
            ret |= Verify(poDS->GetLayer(0), 3000, argv[i]);
        }
        return ret;
    }
    if (mode == "bench")
    {
        std::unique_ptr<GDALDataset> poDS(
            GDALDataset::Open(argv[2], GDAL_OF_VECTOR));
        if (!poDS)
            return 1;
        const int n = atoi(argv[3]);
        for (int i = 4; i < argc; ++i)
            Bench(poDS->GetLayer(0), n, atof(argv[i]));
        return 0;
    }
    return 2;
}
