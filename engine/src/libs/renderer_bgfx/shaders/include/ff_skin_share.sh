// The skin share of a character's pixel, which the relief, parallax, cavity and gloss fade out by (the CPU check
// fx/tests/ff_skin_share_check.cpp includes this file). `boneHead`: the share of the head, neck and hand bones
// (ff_vertex.sh; 0 on every draw that is not skinned); half a bone's share (a collar, a cuff) already takes all of it.
// `boneTorso`: the share of the torso and arm bones, skin where the atlas there has the character's own skin colour:
// `toneKnown` is u_fxSkinTone[0].x (0 disabled, -1 unknown colour: the torso is masked), `toneLikeness` the atlas's
// likeness to it there (read only when the colour is known).
float ffSkinShare(float boneHead, float boneTorso, float toneKnown, float toneLikeness)
{
    float share = clamp(2.0 * boneHead, 0.0, 1.0);
    if (boneTorso > 0.004)
    {
        if (toneKnown < 0.0)
            share = max(share, clamp(2.0 * boneTorso, 0.0, 1.0));
        else if (toneKnown > 0.0)
            share = max(share, clamp(2.0 * boneTorso * toneLikeness, 0.0, 1.0));
    }
    return share;
}
