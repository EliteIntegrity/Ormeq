"""Synthetic pixel and recipe checks for the optional sprite converter."""
import tempfile
from pathlib import Path
import unittest
import numpy as np
from PIL import Image
from build_sprite_pack import portrait, wall, source_cell, build, sha


class SpriteTests(unittest.TestCase):
    def test_transparent_background_cannot_bleed_into_foreground(self):
        pixels=np.zeros((16,16,4),dtype=np.uint8)
        pixels[:,:,:3]=[255,0,255]
        pixels[4:12,7:9]=[20,180,40,255]
        for size in (32,64):
            result=np.array(portrait(Image.fromarray(pixels),size))
            foreground=result[result[:,:,3]>0]
            self.assertTrue(np.all(foreground[:,:3]==[20,180,40]))
            self.assertTrue(np.all(foreground[:,3]==255))
            self.assertFalse(result[0].any() or result[-1].any() or result[:,0].any() or result[:,-1].any())
            self.assertLess(np.count_nonzero(result[:,:,3].any(0)),np.count_nonzero(result[:,:,3].any(1)))

    def test_box_average_and_empty_source(self):
        source=Image.new('RGBA',(4,2),(20,40,60,255))
        for x in (2,3):
            for y in (0,1): source.putpixel((x,y),(100,120,140,255))
        result=portrait(source,3)
        self.assertEqual(result.getpixel((1,1)),(60,80,100,255))
        self.assertIsNone(portrait(Image.new('RGBA',(8,8)),32).getbbox())

    def test_wall_opposite_edges_match(self):
        pixels=np.arange(12*12*3,dtype=np.uint16).reshape((12,12,3)).astype(np.uint8)
        for size in (32,64):
            result=np.array(wall(Image.fromarray(pixels),size))
            np.testing.assert_array_equal(result[:,0],result[:,-1])
            np.testing.assert_array_equal(result[0],result[-1])
            self.assertTrue(np.all(result[:,:,3]==255))

    def test_atlas_cells_preserve_pixels_and_do_not_bleed(self):
        # Odd dimensions exercise the integer cell boundaries used in recipes.
        source=Image.new('RGBA',(7,9),(10,20,30,255))
        source.paste((200,40,50,255),(3,4,7,9))
        cell=source_cell(source,[1,1,2,2])
        self.assertEqual(cell.size,(4,5))
        self.assertEqual(source_cell(source,crop=[3,4,7,9]).tobytes(),cell.tobytes())
        for size in (32,64):
            pixels=np.array(portrait(cell,size))
            self.assertTrue(np.all(pixels[pixels[:,:,3]>0,:3]==[200,40,50]))
        for bad in ([],[0,0,0,2],[-1,0,2,2],[2,0,2,2],[0,0,8,2],
                    [0,0,2,10],[True,0,2,2],[0.5,0,2,2]):
            with self.assertRaises(ValueError): source_cell(source,bad)
        for bad in ([],[-1,0,4,4],[0,0,8,9],[0,0,2,10],[1,1,1,2],[0.,0,4,4]):
            with self.assertRaises(ValueError): source_cell(source,crop=bad)
        with self.assertRaises(ValueError): source_cell(source,[0,0,2,2],[0,0,4,4])

    def test_recipe_is_explicit_reproducible_and_rejects_changed_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); source=root/'owned.png'
            Image.new('RGBA',(8,8),(10,20,30,255)).save(source)
            entry=dict(domain='monster',keys=['sample'],source='owned.png',sha256=sha(source.read_bytes()))
            first=build(root,[entry]); self.assertEqual(first,build(root,[entry]))
            self.assertIn(b'sample 0 0',first['monster-1.txt'])
            with self.assertRaises(ValueError): build(root,[entry,entry])
            with self.assertRaises(ValueError): build(root,[dict(entry,source='../outside.png')])
            with self.assertRaises(ValueError): build(root,[dict(entry,kind='typo')])
            subset=build(root,[dict(entry,keys=['sample','sample-alias'],cell=[0,0,2,2])])
            self.assertIn(b'sample-alias 0 0',subset['monster-1.txt'])
            self.assertIn(b'"cell": [',subset['inventory.json'])
            source.write_bytes(b'changed')
            with self.assertRaises(ValueError): build(root,[entry])


if __name__ == '__main__':
    unittest.main()
