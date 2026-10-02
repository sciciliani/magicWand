# Wizard art

The Training tab shows the wizard by mood. Each mood can have several images; they rotate:

| Mood | Files | When |
|---|---|---|
| welcome | `welcome.png` | Start and end of the training |
| talk | `talk1.png`, `talk2.png`, `talk3.png` | Explaining and watching you |
| yes | `yes.png`, `yes2.png` | The move was right |
| no | `no.png`, `no2.png` | Try again |

Only PNGs that exist are used (missing ones are skipped). Without any PNG the `.svg` placeholders are shown.
Portrait, transparent background, ~900 px tall.

The current set was cut out of one Gemini sheet (6 poses + 3 backgrounds), the grey removed and upscaled ×4
with Real-ESRGAN (anime 6B model). Backgrounds: `../scenes/workshop.jpg` (Training), `observatory.jpg`
(Summon window), `wandcase.jpg` (Spells).
