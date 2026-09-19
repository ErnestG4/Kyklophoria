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

.PHONY: all clean corpus align bake grade renders all-stages
all: build/modalfem build/align build/bake build/grade build/render
all-stages: corpus align bake grade renders

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
MESHES   = $(if $(wildcard meshes/*.tet),$(wildcard meshes/*.tet),$(foreach f,bar plate bell tine,$(foreach i,00 01 02 03 04 05 06 07 08 09 10 11,meshes/$(f)$(i).tet)))
RECORDS  = $(patsubst meshes/%.tet,out/mmr/%.mmr,$(MESHES))
NMODES  ?= 48

# Grouped targets (&:), so that one run of the generator satisfies every mesh
# and a later make does not regenerate them — and then, because they would be
# newer, rerun all thirty-six FEMs. It did that once.
meshes/meshes.tsv $(MESHES) $(patsubst %.tet,%.expos,$(MESHES)) $(patsubst %.tet,%.opts,$(MESHES)) &: tools/meshgen.py
	python3 tools/meshgen.py meshes

out/mmr/%.mmr: meshes/%.tet meshes/%.expos build/modalfem
	@mkdir -p out/mmr
	build/modalfem --tet $< --expos meshes/$*.expos --out $@ --nmodes $(NMODES) --quiet $$(cat meshes/$*.opts 2>/dev/null)

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
# On the brief's corpus — the three families — so the findings stay the
# findings as the corpus grows; families added since get their own spaces
# below.
EXTENT ?= 2.2
BRIEF  ?= bar,plate,bell
out/space-%.msp out/bake-%.txt: build/bake out/corpus.mdb out/align.bin
	build/bake out/corpus.mdb out/align.bin out/space-$*.msp out/bake-$*.txt --variant $* --extent $(EXTENT) --family $(BRIEF)

bake: out/space-full.msp out/space-lambda.msp out/space-linear.msp out/space-gonly.msp

build/grade: tools/grade/main.cpp $(COMMON) tools/common/linalg.h tools/common/space.h tools/common/spectrum.h
	@mkdir -p build
	$(CXX) $(FLAGS) tools/grade/main.cpp -o $@

# ── Stage 4/5: the grade, and the three numbers ──────────────────────────────
GRADEFLAGS ?=
out/grade-%.txt: build/grade out/space-%.msp out/corpus.mdb out/align.bin
	build/grade out/space-$*.msp out/corpus.mdb out/align.bin $@ $(GRADEFLAGS)

# the pitch-normalised grade, beside the brief's
out/grade-%-np.txt: build/grade out/space-%.msp out/corpus.mdb out/align.bin
	build/grade out/space-$*.msp out/corpus.mdb out/align.bin $@ --normalise-pitch $(GRADEFLAGS)

VARIANTS = full lambda linear gonly
grade: $(patsubst %,out/grade-%.txt,$(VARIANTS)) $(patsubst %,out/grade-%-np.txt,$(VARIANTS))
	@echo; for v in $(VARIANTS); do printf "%-7s " $$v; grep -h "^spread" out/grade-$$v.txt | cut -c1-14 | tr -d '\n'; printf "  pitch-normalised "; grep -h "^spread" out/grade-$$v-np.txt | cut -c1-14; done

build/render: tools/render/main.cpp $(COMMON) tools/common/linalg.h tools/common/space.h
	@mkdir -p build
	$(CXX) $(FLAGS) tools/render/main.cpp -o $@

# ── the listening set ────────────────────────────────────────────────────────
# Eight paths through the full space, plus the same two cross-family paths
# through the frequency-only space, which is what the go/no-go is about.
RENDERS = out/wav/bar-sweep.wav out/wav/plate-sweep.wav out/wav/bell-sweep.wav \
          out/wav/bar-to-bell.wav out/wav/plate-to-bar.wav out/wav/bell-to-plate.wav \
          out/wav/plate-veering.wav out/wav/bar-to-bell-via-plate.wav \
          out/wav/bar-to-bell-lambda-only.wav out/wav/plate-to-bar-lambda-only.wav
renders: $(RENDERS)
out/wav/bar-sweep.wav: build/render out/space-full.msp
	@mkdir -p out/wav
	build/render out/space-full.msp out/corpus.mdb out/align.bin $@ --from bar00 --to bar11
out/wav/plate-sweep.wav: build/render out/space-full.msp
	build/render out/space-full.msp out/corpus.mdb out/align.bin $@ --from plate00 --to plate11
out/wav/bell-sweep.wav: build/render out/space-full.msp
	build/render out/space-full.msp out/corpus.mdb out/align.bin $@ --from bell00 --to bell11
out/wav/bar-to-bell.wav: build/render out/space-full.msp
	build/render out/space-full.msp out/corpus.mdb out/align.bin $@ --from bar05 --to bell05
out/wav/plate-to-bar.wav: build/render out/space-full.msp
	build/render out/space-full.msp out/corpus.mdb out/align.bin $@ --from plate05 --to bar05
out/wav/bell-to-plate.wav: build/render out/space-full.msp
	build/render out/space-full.msp out/corpus.mdb out/align.bin $@ --from bell05 --to plate05
# the square plate's degenerate pairs splitting: aspect 1.0 to 1.55, where the
# veering map puts every jump over 300 cents the plate sweep has
out/wav/plate-veering.wav: build/render out/space-full.msp
	build/render out/space-full.msp out/corpus.mdb out/align.bin $@ --from plate00 --to plate03 --strikes 32 --interval 0.15
out/wav/bar-to-bell-via-plate.wav: build/render out/space-full.msp
	build/render out/space-full.msp out/corpus.mdb out/align.bin $@ --from bar05 --to bell05 --via plate05 --strikes 32
out/wav/bar-to-bell-lambda-only.wav: build/render out/space-lambda.msp
	build/render out/space-lambda.msp out/corpus.mdb out/align.bin $@ --from bar05 --to bell05
out/wav/plate-to-bar-lambda-only.wav: build/render out/space-lambda.msp
	build/render out/space-lambda.msp out/corpus.mdb out/align.bin $@ --from plate05 --to bar05

# ── one family at a time ─────────────────────────────────────────────────────
FAMILIES = bar plate bell tine
out/space-fam-%.msp out/bake-fam-%.txt: build/bake out/corpus.mdb out/align.bin
	build/bake out/corpus.mdb out/align.bin out/space-fam-$*.msp out/bake-fam-$*.txt --variant full --family $* --extent $(EXTENT)
out/space-famlambda-%.msp out/bake-famlambda-%.txt: build/bake out/corpus.mdb out/align.bin
	build/bake out/corpus.mdb out/align.bin out/space-famlambda-$*.msp out/bake-famlambda-$*.txt --variant lambda --family $* --extent $(EXTENT)
out/space-famgonly-%.msp out/bake-famgonly-%.txt: build/bake out/corpus.mdb out/align.bin
	build/bake out/corpus.mdb out/align.bin out/space-famgonly-$*.msp out/bake-famgonly-$*.txt --variant gonly --family $* --extent $(EXTENT)
FAMSPACES = $(foreach f,$(FAMILIES),out/space-fam-$(f).msp out/space-famlambda-$(f).msp out/space-famgonly-$(f).msp)
families: $(FAMSPACES) build/grade
	@for s in $(FAMSPACES); do n=$$(basename $$s .msp | sed 's/^space-//'); \
	  build/grade $$s out/corpus.mdb out/align.bin out/grade-$$n.txt --normalise-pitch $(GRADEFLAGS) | sed "s/^grade [a-z]*/$$n/"; done

# ── the second listening set: glides and walks ──────────────────────────────
GLIDES = out/wav/glide-bar-to-bell.wav out/wav/glide-plate-veering.wav out/wav/walk-full-1.wav \
         out/wav/walk-full-2.wav out/wav/walk-plate.wav out/wav/walk-bell.wav out/wav/walk-bar.wav
glides: $(GLIDES)
out/wav/glide-bar-to-bell.wav: build/render out/space-full.msp
	build/render out/space-full.msp out/corpus.mdb out/align.bin $@ --from bar05 --to bell05 --mode glide --strikes 24 --interval 0.5
out/wav/glide-plate-veering.wav: build/render out/space-full.msp
	build/render out/space-full.msp out/corpus.mdb out/align.bin $@ --from plate00 --to plate03 --mode glide --strikes 16 --interval 0.5
out/wav/walk-full-1.wav: build/render out/space-full.msp
	build/render out/space-full.msp out/corpus.mdb out/align.bin $@ --walk 1 --seconds 16 --mode glide --interval 0.4
out/wav/walk-full-2.wav: build/render out/space-full.msp
	build/render out/space-full.msp out/corpus.mdb out/align.bin $@ --walk 2 --seconds 16 --mode glide --interval 0.25 --strike 5 --listen 5
out/wav/walk-plate.wav: build/render out/space-fam-plate.msp
	build/render out/space-fam-plate.msp out/corpus.mdb out/align.bin $@ --walk 3 --seconds 16 --mode glide --interval 0.3
out/wav/walk-bell.wav: build/render out/space-fam-bell.msp
	build/render out/space-fam-bell.msp out/corpus.mdb out/align.bin $@ --walk 4 --seconds 16 --mode glide --interval 0.5
out/wav/walk-bar.wav: build/render out/space-fam-bar.msp
	build/render out/space-fam-bar.msp out/corpus.mdb out/align.bin $@ --walk 5 --seconds 12 --mode glide --interval 0.2
out/wav/walk-tine.wav: build/render out/space-fam-tine.msp
	build/render out/space-fam-tine.msp out/corpus.mdb out/align.bin $@ --walk 6 --seconds 16 --mode glide --interval 0.35 --strike 11 --listen 11
out/wav/tine-sweep.wav: build/render out/space-fam-tine.msp
	build/render out/space-fam-tine.msp out/corpus.mdb out/align.bin $@ --from tine00 --to tine11 --strikes 24 --interval 0.3 --strike 11 --listen 11
GLIDES += out/wav/walk-tine.wav out/wav/tine-sweep.wav
