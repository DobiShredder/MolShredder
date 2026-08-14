# Volumetric data foundation

상태: typed scalar grid와 OpenDX/MRC2014/CCP4 read-only vertical slice

MolShredder의 volume은 molecular coordinate frame에 억지로 넣지 않는 독립 data model이다.
`model::VolumeGrid`는 다음 값을 소유한다.

- 양수인 `x/y/z` dimension
- Cartesian origin과 서로 독립인 세 delta vector
- `float32` 또는 `float64` scalar buffer
- coordinate unit, optional scalar unit와 source metadata

Delta는 축 정렬을 요구하지 않으므로 skewed regular grid도 보존한다. 세 vector의 basis가 퇴화하거나
origin/delta/scalar가 non-finite이면 object를 만들지 않는다. Scalar의 linear order는 OpenDX와 같은
z-fastest order다.

```text
index(x, y, z) = (x * count_y + y) * count_z + z
position(x, y, z) = origin + x * delta_x + y * delta_y + z * delta_z
```

## OpenDX contract

현재 reader는 APBS가 출력하는 ASCII regular scalar subset을 지원한다.

- `gridpositions counts nx ny nz`
- `origin ox oy oz`
- 세 개의 `delta` vector
- 동일 dimension의 `gridconnections`
- `array type float|double rank 0 items N data follows`
- optional field/component/attribute record

Quoted object identifier와 scalar data가 array declaration 뒤 같은 줄에 시작하는 경우를 지원한다.
Declared dimension, connection count, item count와 실제 scalar 수가 모두 정확히 일치해야 한다.
Reader는 512 MiB file, 100,000,000 voxel과 1 MiB line 상한을 적용하고 overflow를 object allocation 전에
거부한다.

OpenDX regular grid에는 coordinate unit field가 없으므로 기본값은 APBS convention에 맞춘 Angstrom이다.
다른 convention의 파일은 `--coordinate-unit nanometer`처럼 명시해야 하며 결과 metadata가 이 선택을
기록한다. Scalar unit은 OpenDX syntax만으로 추정하지 않는다.

```text
invoke "volume load" --path "potential.dx" --name "electrostatic" \
  --file-format "opendx" --coordinate-unit "angstrom"
invoke "volume list"
invoke "format list" --family "volume"
```

Python은 동일 command registry를 사용한다.

```python
import molshredder

result = molshredder.invoke("volume load", {
    "path": "potential.dx",
    "name": "electrostatic",
    "file-format": "opendx",
    "coordinate-unit": "angstrom",
})
```

Desktop Open dialog도 `.dx`를 같은 operation으로 읽는다. 현재는 dimensions와 voxel count를 status에
표시하고 typed volume scene node를 유지하지만 화면 geometry를 생성하지 않는다.

## MRC2014/CCP4 contract

MRC/CCP4 reader는 1024-byte main header, `NSYMBT` extended-header offset과 뒤따르는 3-D data block을
읽는다. `.mrc`, `.mrcs`, `.ccp4`와 MRC magic을 가진 `.map`을 지원한다. `.map`은 여러 format이 공유하는
확장자이므로 suffix만으로 MRC라고 단정하지 않고 `MAP ` identifier를 검사한다.

- little/big-endian `MACHST`; legacy unknown stamp는 한 byte order만 유효할 때만 추론
- scalar mode 0 signed int8, 1 signed int16, 2 float32, 6 unsigned int16, 12 IEEE binary16
- column/row/section fastness와 `MAPC/MAPR/MAPS` permutation
- `MX/MY/MZ`, cell length/angle로 만든 triclinic X/Y/Z delta vector
- nonzero MRC `ORIGIN` 우선, 그렇지 않으면 permuted `NXSTART/NYSTART/NZSTART`에서 계산한 origin
- Angstrom header geometry와 optional nanometer output conversion
- version, mode, byte order, cell, space group, extended-header type/size, labels와 header statistics provenance

Disk data는 column-fastest지만 `VolumeGrid`에는 logical X/Y/Z z-fastest 순서로 재배열한다. Header
density statistics는 오래됐을 수 있으므로 보존만 하고 실제 scalar range는 decoded data에서 계산한다.
Nonzero ORIGIN과 grid-start origin이 다르면 ORIGIN을 사용하면서 conflict와 두 값을 metadata에 남긴다.

```text
invoke "volume load" --path "density.mrc" --name "density" \
  --file-format "mrc" --coordinate-unit "angstrom"
invoke "volume load" --path "difference.ccp4" --name "difference" \
  --file-format "ccp4"
```

MRC2014는 data block handedness를 보편적으로 확정하지 않으므로 reader가 임의 축 반전을 수행하지 않는다.
`mrc_handedness=unspecified_by_standard`를 보존하며 handedness override와 visual validation은 후속 기능이다.

## 명시적 한계

다음 입력이나 기능은 아직 지원하지 않는다.

- binary OpenDX, irregular positions, vector/tensor array와 finite-element field
- 한 파일의 여러 grid/array/field 및 external data payload
- OpenDX writer와 streaming/out-of-core scalar storage
- XPLOR/CNS, cube, DSN6/BRIX, Situs, CHGCAR
- MRC complex transform mode 3/4, packed 4-bit mode 101과 CCP4 skew transform
- MRC extended-header payload 해석, image/volume-stack 분리와 handedness override
- isosurface, mesh, slice, field line와 direct volume ray marching
- map arithmetic/resampling, electrostatic potential surface coloring과 trajectory-dependent volume
- volume object의 session persistence, rename/delete/visibility command

지원 여부는 extension 이름이 아니라 `format list --family volume`의 channel/direction/limitation과
conformance fixture를 기준으로 판단한다.

## Normative reference

- [APBS OpenDX scalar data format](https://apbs.readthedocs.io/en/stable/formats/opendx.html)
- [CCP-EM MRC2014 specification](https://www.ccpem.ac.uk/mrc-format/mrc2014/)
- [CCP4 MAPLIB map-format documentation](https://www.ccp4.ac.uk/html/maplib.html)

Synthetic OpenDX fixture는 MolShredder용으로 직접 작성했다.
