// objexp <model> <outdir> [node...]: world-space OBJ per requested node subtree (or every top-level node),
// embedded textures saved as tex<N>.<ext>, material -> base color texture listed in <outdir>/mats.txt
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
#include <set>
int main(int argc, char** argv) {
	Assimp::Importer imp;
	const aiScene* s = imp.ReadFile(argv[1], aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices * 0);
	if (!s) { printf("load failed: %s\n", imp.GetErrorString()); return 1; }
	std::string out = argv[2];
	for (unsigned t = 0; t < s->mNumTextures; ++t) {
		auto* tx = s->mTextures[t];
		if (tx->mHeight == 0) { std::ofstream f(out + "/tex" + std::to_string(t) + "." + tx->achFormatHint, std::ios::binary); f.write((const char*)tx->pcData, tx->mWidth); }
	}
	{
		std::ofstream m(out + "/mats.txt");
		for (unsigned i = 0; i < s->mNumMaterials; ++i) {
			auto* mt = s->mMaterials[i]; aiString n, p; mt->Get(AI_MATKEY_NAME, n);
			m << i << " " << n.C_Str();
			for (auto t : { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE, aiTextureType_EMISSIVE, aiTextureType_NORMALS })
				if (mt->GetTexture(t, 0, &p) == AI_SUCCESS) m << " " << (int)t << "=" << p.C_Str();
			aiColor4D c; if (mt->Get(AI_MATKEY_BASE_COLOR, c) == AI_SUCCESS || mt->Get(AI_MATKEY_COLOR_DIFFUSE, c) == AI_SUCCESS) m << " col=" << c.r << "," << c.g << "," << c.b;
			aiColor3D e; if (mt->Get(AI_MATKEY_COLOR_EMISSIVE, e) == AI_SUCCESS) m << " emit=" << e.r << "," << e.g << "," << e.b;
			m << "\n";
		}
	}
	std::vector<aiNode*> roots;
	std::function<aiNode*(aiNode*, const char*)> find = [&](aiNode* n, const char* name) -> aiNode* {
		if (std::string(n->mName.C_Str()) == name) return n;
		for (unsigned c = 0; c < n->mNumChildren; ++c) if (auto r = find(n->mChildren[c], name)) return r;
		return nullptr; };
	if (argc > 3) for (int i = 3; i < argc; ++i) { if (auto r = find(s->mRootNode, argv[i])) roots.push_back(r); else printf("no node %s\n", argv[i]); }
	else for (unsigned c = 0; c < s->mRootNode->mNumChildren; ++c) roots.push_back(s->mRootNode->mChildren[c]);
	for (auto* root : roots) {
		std::string name = root->mName.C_Str(); for (auto& ch : name) if (ch == ' ' || ch == '/') ch = '_';
		std::ofstream f(out + "/" + name + ".obj");
		size_t vo = 1; aiVector3D lo(1e9,1e9,1e9), hi(-1e9,-1e9,-1e9);
		std::function<void(aiNode*, aiMatrix4x4)> walk = [&](aiNode* n, aiMatrix4x4 parent) {
			aiMatrix4x4 w = parent * n->mTransformation;
			for (unsigned k = 0; k < n->mNumMeshes; ++k) {
				auto* me = s->mMeshes[n->mMeshes[k]];
				f << "o " << me->mName.C_Str() << "\nusemtl m" << me->mMaterialIndex << "\n";
				for (unsigned v = 0; v < me->mNumVertices; ++v) { auto p = w * me->mVertices[v]; f << "v " << p.x << " " << p.y << " " << p.z << "\n";
					lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z); }
				for (unsigned v = 0; v < me->mNumVertices; ++v) { auto t = me->HasTextureCoords(0) ? me->mTextureCoords[0][v] : aiVector3D(); f << "vt " << t.x << " " << t.y << "\n"; }
				for (unsigned fc = 0; fc < me->mNumFaces; ++fc) { auto& F = me->mFaces[fc]; if (F.mNumIndices != 3) continue;
					f << "f"; for (int j = 0; j < 3; ++j) f << " " << F.mIndices[j] + vo << "/" << F.mIndices[j] + vo; f << "\n"; }
				vo += me->mNumVertices;
			}
			for (unsigned c = 0; c < n->mNumChildren; ++c) walk(n->mChildren[c], w);
		};
		// parent chain transform
		aiMatrix4x4 pw; for (aiNode* p = root->mParent; p; p = p->mParent) pw = p->mTransformation * pw;
		walk(root, pw);
		printf("%s: %zu verts bounds (%.2f %.2f %.2f)..(%.2f %.2f %.2f)\n", name.c_str(), vo - 1, lo.x,lo.y,lo.z,hi.x,hi.y,hi.z);
	}
}
