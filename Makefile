# ModalBake — host build. Measurement tools, nothing that runs in real time.
#
#   make            every tool into build/
#   make corpus     Stage 1: meshes -> modalfem -> out/corpus.mdb (+ manifest)
#   make align      Stage 2: out/align.txt
#   make bake       Stage 3: out/space-*.msp + reconstruction report
#   make grade      Stage 4/5: out/grade.txt, the ablation table, veering map
#   make renders    the listening set, out/wav/
#   make all-stages corpus align bake grade renders, in order, with checkpoints
#
# Dependencies live beside this repo, not in it: ../faust (GRAME's, for
# mesh2faust's Vega and Spectra) and ../eigen (header-only). Neither is modified.
CXX      ?= g++
FAUST    ?= ../faust
EIGEN    ?= ../eigen
M2F       = $(FAUST)/tools/physicalModeling/mesh2faust
VEGA      = $(M2F)/vega/libraries
FLAGS     = -std=gnu++17 -O2 -Wall -Wextra
# Vega's objMesh sources include GLUT for render functions nobody calls; an
# empty glut.h in shim/ stands in for a package this machine cannot install.
FEMFLAGS  = -std=gnu++14 -O2 -w -Ishim -I$(EIGEN) -I$(M2F)/spectra/include -I$(VEGA)/include
VEGASRC   = $(VEGA)/objMesh/objMesh.cpp $(VEGA)/objMesh/objMesh-disjointSet.cpp \
            $(VEGA)/objMesh/objMeshOctree.cpp $(VEGA)/objMesh/objMeshOrientable.cpp \
            $(VEGA)/objMesh/boundingBox.cpp $(VEGA)/objMesh/triangle.cpp $(VEGA)/objMesh/tribox3.cpp \
            $(VEGA)/objMesh/octree.cpp $(VEGA)/objMesh/simpleSphere.cpp \
            $(VEGA)/volumetricMesh/volumetricMesh.cpp $(VEGA)/volumetricMesh/volumetricMeshParser.cpp \
            $(VEGA)/volumetricMesh/volumetricMeshENuMaterial.cpp \
            $(VEGA)/volumetricMesh/volumetricMeshOrthotropicMaterial.cpp \
            $(VEGA)/volumetricMesh/volumetricMeshMooneyRivlinMaterial.cpp \
            $(VEGA)/volumetricMesh/cubicMesh.cpp $(VEGA)/volumetricMesh/tetMesh.cpp \
            $(VEGA)/volumetricMesh/generateMassMatrix.cpp \
            $(VEGA)/mesher/tetMesher.cpp $(VEGA)/mesher/delaunayMesher.cpp \
            $(VEGA)/mesher/triangleTetIntersection.cpp \
            $(VEGA)/sparseMatrix/sparseMatrix.cpp \
            $(VEGA)/stvk/StVKElementABCD.cpp $(VEGA)/stvk/StVKElementABCDLoader.cpp \
            $(VEGA)/stvk/StVKCubeABCD.cpp $(VEGA)/stvk/StVKTetABCD.cpp \
            $(VEGA)/stvk/StVKTetHighMemoryABCD.cpp $(VEGA)/stvk/StVKInternalForces.cpp \
            $(VEGA)/stvk/StVKStiffnessMatrix.cpp \
            $(VEGA)/wildMagic/rational/integer.cpp $(VEGA)/wildMagic/rational/rational.cpp \
            $(VEGA)/windingNumber/windingNumber.cpp \
            $(VEGA)/wildMagic/geometryQuery/verticesQuery.cpp \
            $(VEGA)/wildMagic/geometryQuery/verticesQueryFiltered.cpp \
            $(VEGA)/wildMagic/geometryQuery/verticesQueryRational.cpp \
            $(VEGA)/wildMagic/meshKey/tetKey.cpp $(VEGA)/wildMagic/meshKey/triKey.cpp
VEGAOBJ   = $(patsubst $(VEGA)/%.cpp,build/vega/%.o,$(VEGASRC))

.PHONY: all clean corpus
all: build/modalfem

build/vega/%.o: $(VEGA)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FEMFLAGS) -c $< -o $@

build/modalfem: tools/modalfem/main.cpp $(VEGAOBJ)
	@mkdir -p build
	$(CXX) $(FEMFLAGS) tools/modalfem/main.cpp $(VEGAOBJ) -lGL -o $@

clean:
	rm -rf build

# ── Stage 1: the corpus ──────────────────────────────────────────────────────
# Each model is an independent FEM, so they run in parallel; the pack step is
# the checkpoint. `make corpus` again does nothing unless a mesh or the
# extractor changed.
MESHES   = $(wildcard meshes/*.tet)
RECORDS  = $(patsubst meshes/%.tet,out/mmr/%.mmr,$(MESHES))
NMODES  ?= 48

meshes/meshes.tsv: tools/meshgen.py
	python3 tools/meshgen.py meshes

out/mmr/%.mmr: meshes/%.tet meshes/%.expos build/modalfem
	@mkdir -p out/mmr
	build/modalfem --tet $< --expos meshes/$*.expos --out $@ --nmodes $(NMODES) --quiet

out/corpus.mdb: $(RECORDS) tools/pack.py meshes/meshes.tsv
	python3 tools/pack.py meshes/meshes.tsv out/mmr out/corpus.mdb out/manifest.tsv --N $(NMODES)

corpus: out/corpus.mdb

# ── the analysis tools: plain C++, no Vega ───────────────────────────────────
COMMON = tools/common/corpus.h tools/common/hungarian.h

build/align: tools/align/main.cpp $(COMMON)
	@mkdir -p build
	$(CXX) $(FLAGS) tools/align/main.cpp -o $@

# ── Stage 2: alignment ───────────────────────────────────────────────────────
REF ?= bar05
out/align.txt out/align.bin: build/align out/corpus.mdb
	build/align out/corpus.mdb out/align.txt out/align.bin --ref $(REF)

align: out/align.txt

build/bake: tools/bake/main.cpp $(COMMON) tools/common/linalg.h tools/common/space.h
	@mkdir -p build
	$(CXX) $(FLAGS) tools/bake/main.cpp -o $@

# ── Stage 3: the bake, three ways ────────────────────────────────────────────
EXTENT ?= 2.2
out/space-%.msp out/bake-%.txt: build/bake out/corpus.mdb out/align.bin
	build/bake out/corpus.mdb out/align.bin out/space-$*.msp out/bake-$*.txt --variant $* --extent $(EXTENT)

bake: out/space-full.msp out/space-lambda.msp out/space-linear.msp
