// inspect <model>: meshes, materials, skeleton, animations (assimp)
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <cstdio>
#include <functional>
int main(int argc, char** argv) {
	Assimp::Importer imp;
	const aiScene* s = imp.ReadFile(argv[1], aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
	if (!s) { printf("load failed: %s\n", imp.GetErrorString()); return 1; }
	printf("meshes %u materials %u animations %u\n", s->mNumMeshes, s->mNumMaterials, s->mNumAnimations);
	aiVector3D lo(1e9,1e9,1e9), hi(-1e9,-1e9,-1e9);
	for (unsigned m = 0; m < s->mNumMeshes; ++m) {
		auto* me = s->mMeshes[m];
		for (unsigned v = 0; v < me->mNumVertices; ++v) { auto p = me->mVertices[v]; lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);}
		aiString mn; s->mMaterials[me->mMaterialIndex]->Get(AI_MATKEY_NAME, mn);
		printf(" mesh %u '%s' verts %u tris %u bones %u uv %d mat '%s'\n", m, me->mName.C_Str(), me->mNumVertices, me->mNumFaces, me->mNumBones, me->HasTextureCoords(0), mn.C_Str());
	}
	printf("bounds (%.2f %.2f %.2f) .. (%.2f %.2f %.2f)\n", lo.x,lo.y,lo.z,hi.x,hi.y,hi.z);
	for (unsigned i = 0; i < s->mNumMaterials; ++i) {
		aiString p; auto* mt = s->mMaterials[i];
		for (auto t : {aiTextureType_DIFFUSE, aiTextureType_NORMALS, aiTextureType_EMISSIVE, aiTextureType_BASE_COLOR})
			if (mt->GetTexture(t, 0, &p) == AI_SUCCESS) printf(" mat %u tex(%d) %s\n", i, (int)t, p.C_Str());
	}
	int depthLimit = argc > 2 ? atoi(argv[2]) : 3; int count = 0;
	std::function<void(aiNode*, int)> walk = [&](aiNode* n, int d) {
		++count;
		if (d <= depthLimit) printf("%*s%s%s\n", d*2, "", n->mName.C_Str(), n->mNumMeshes ? " [mesh]" : "");
		for (unsigned c = 0; c < n->mNumChildren; ++c) walk(n->mChildren[c], d + 1);
	};
	walk(s->mRootNode, 0);
	printf("nodes %d\n", count);
	for (unsigned a = 0; a < s->mNumAnimations; ++a) printf(" anim '%s' %.1f ticks @%.1f channels %u\n", s->mAnimations[a]->mName.C_Str(), s->mAnimations[a]->mDuration, s->mAnimations[a]->mTicksPerSecond, s->mAnimations[a]->mNumChannels);
}
